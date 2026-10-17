#include "audio_engine.h"
#include "equalizer.h"
#include "../third_party/miniaudio/miniaudio.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
void U16(std::vector<unsigned char>& data, std::uint16_t value) { data.push_back(value & 255); data.push_back(value >> 8); }
void U32(std::vector<unsigned char>& data, std::uint32_t value) { U16(data, value & 65535); U16(data, value >> 16); }
void Tag(std::vector<unsigned char>& data, const char* text) { for (int i = 0; i < 4; ++i) data.push_back(text[i]); }
std::vector<unsigned char> Wave() {
    constexpr std::uint32_t frames = 48000;
    std::vector<unsigned char> bytes;
    Tag(bytes, "RIFF"); U32(bytes, frames * 4 + 36); Tag(bytes, "WAVE"); Tag(bytes, "fmt ");
    U32(bytes, 16); U16(bytes, 1); U16(bytes, 2); U32(bytes, 48000); U32(bytes, 192000); U16(bytes, 4); U16(bytes, 16);
    Tag(bytes, "data"); U32(bytes, frames * 4);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto sample = static_cast<std::int16_t>(std::sin(i * 6.283185307179586 * 1000 / 48000) * 4000);
        U16(bytes, static_cast<std::uint16_t>(sample)); U16(bytes, static_cast<std::uint16_t>(sample));
    }
    return bytes;
}
std::vector<unsigned char> SilentMp3() {
    // Independent synthetic MPEG-1 Layer III 128 kb/s, 44.1 kHz stereo frames.
    // Zero side info sets every part2_3_length to zero: both granules are silence.
    std::vector<unsigned char> bytes(417 * 40);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 417) {
        bytes[offset] = 0xff; bytes[offset + 1] = 0xfb; bytes[offset + 2] = 0x90;
    }
    return bytes;
}
std::vector<unsigned char> ConstantFlac() {
    // One independently constructed constant-subframe FLAC frame, 1024 frames.
    std::vector<unsigned char> bytes{'f', 'L', 'a', 'C', 0x80, 0, 0, 34};
    bytes.insert(bytes.end(), {4, 0, 4, 0, 0, 0, 0, 0, 0, 0});
    const std::uint64_t streamInfo = (std::uint64_t(48000) << 44) | (std::uint64_t(1) << 41) | (std::uint64_t(15) << 36) | 1024;
    for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(streamInfo >> shift));
    bytes.insert(bytes.end(), 16, 0); // No source MD5 declared.
    std::vector<unsigned char> frame{0xff, 0xf8, 0xaa, 0x18, 0};
    unsigned crc8 = 0;
    for (unsigned char byte : frame) { crc8 ^= byte; for (int bit = 0; bit < 8; ++bit) crc8 = (crc8 & 128) ? ((crc8 << 1) ^ 7) & 255 : (crc8 << 1) & 255; }
    frame.push_back(static_cast<unsigned char>(crc8));
    frame.insert(frame.end(), {0, 0x10, 0, 0, 0x10, 0}); // Constant +4096 in each signed-16 channel.
    unsigned crc16 = 0;
    for (unsigned char byte : frame) { crc16 ^= unsigned(byte) << 8; for (int bit = 0; bit < 8; ++bit) crc16 = (crc16 & 32768) ? ((crc16 << 1) ^ 0x8005) & 65535 : (crc16 << 1) & 65535; }
    frame.push_back(static_cast<unsigned char>(crc16 >> 8)); frame.push_back(static_cast<unsigned char>(crc16));
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    return bytes;
}
double Rms(const std::vector<float>& samples, std::size_t begin = 0) {
    double sum = 0;
    for (std::size_t i = begin; i < samples.size(); ++i) sum += samples[i] * samples[i];
    return std::sqrt(sum / (samples.size() - begin));
}
int HardwareSmoke(const std::filesystem::path& path) {
    mdamp::AudioEngine engine;
    std::wstring error;
    Check(engine.Open(path.wstring(), error), "hardware open failed");
    Check(engine.Snapshot().duration > .5, "smoke fixture must be at least 0.5 seconds");
    engine.SetVolume(.05f); // Quiet synthetic test, never modify system mixer.
    Check(engine.Play(), "hardware play failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto first = engine.Snapshot();
    Check(first.position > 0 && first.position < first.duration, "WASAPI callback did not advance PCM cursor");
    Check(*std::max_element(first.spectrum.begin(), first.spectrum.end()) > 0, "PCM spectrum has no energy");
    engine.Pause();
    const auto paused = engine.Snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    Check(engine.Snapshot().position == paused.position, "pause did not retain cursor");
    engine.Seek(.1); Check(std::abs(engine.Snapshot().position - .1) < .001, "seek failed");
    Check(engine.Play(), "resume failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    Check(engine.Snapshot().position > .1, "resume cursor did not advance");
    engine.Stop(); Check(engine.Snapshot().position == 0 && !engine.Snapshot().playing, "stop failed");
    engine.Seek(engine.Snapshot().duration - .05); Check(engine.Play(), "end-test play failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check(engine.Snapshot().ended && !engine.Snapshot().playing, "natural end not detected");
    Check(engine.Play(), "replay after end failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    Check(engine.Snapshot().position < .5, "replay did not restart from beginning");
    engine.Stop();
    std::atomic<bool> observing{true};
    std::atomic<unsigned> observations{0};
    std::thread observer([&] {
        while (observing.load()) {
            (void)engine.Snapshot();
            engine.SetVolume(.05f);
            engine.SetPan(0);
            observations.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    bool reopened = true;
    for (unsigned iteration = 0; iteration < 4; ++iteration) {
        if (!engine.Open(path.wstring(), error) || !engine.Play()) { reopened = false; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        engine.Pause();
    }
    observing.store(false);
    observer.join();
    Check(reopened && observations.load() > 0, "concurrent snapshot/settings and repeated device reopen failed");
    engine.Stop();
    engine.Close();
    Check(engine.Snapshot().duration == 0 && !engine.Play(), "close must unload media, not merely stop");
    engine.Close(); // Idempotent, also valid before the first Open.
    Check(engine.Open(path.wstring(), error) && engine.Play(), "explicit close then reopen must work");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    Check(engine.Snapshot().position > 0, "reopened engine must render again");
    engine.Close();
    std::cout << "WASAPI PCM callback, energy, pause, seek, resume, stop, end and replay passed. This is not acoustic loopback verification.\n";
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const auto wave = Wave();
        if (argc == 3 && std::wstring(argv[1]) == L"--write-fixture") {
            std::ofstream file(std::filesystem::path(argv[2]), std::ios::binary);
            file.write(reinterpret_cast<const char*>(wave.data()), static_cast<std::streamsize>(wave.size()));
            Check(bool(file), "fixture write failed"); return 0;
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--audio-smoke") return HardwareSmoke(argv[2]);
        mdamp::AudioEngine engine;
        Check(!engine.Snapshot().playing && engine.Snapshot().duration == 0, "constructor must remain idle");
        Check(!engine.Play(), "play without file must fail");
        std::wstring error;
        Check(!engine.Open(L"", error) && !error.empty(), "empty path must produce error");
        Check(!engine.Open(L"mdamp-nonexistent-audio-fixture-35821.wav", error) && !error.empty(), "missing file must produce error");
        engine.Pause(); engine.Stop(); engine.Seek(-1); engine.Seek(std::numeric_limits<double>::quiet_NaN());
        engine.Close(); engine.Close();
        engine.SetVolume(2); engine.SetPan(-2); engine.SetDspCallback(nullptr, nullptr);

        ma_decoder decoder{};
        const auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
        Check(ma_decoder_init_memory(wave.data(), wave.size(), &config, &decoder) == MA_SUCCESS, "offline WAV decoder init");
        ma_uint64 length = 0, read = 0;
        Check(ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length == 48000, "decoded WAV duration");
        std::vector<float> pcm(96000);
        Check(ma_decoder_read_pcm_frames(&decoder, pcm.data(), 48000, &read) == MA_SUCCESS && read == 48000, "decode all PCM frames");
        Check(Rms(pcm) > .08 && Rms(pcm) < .09, "decoded PCM amplitude");
        Check(ma_decoder_seek_to_pcm_frame(&decoder, 24000) == MA_SUCCESS, "offline decoder seek");
        ma_decoder_uninit(&decoder);

        const auto mp3 = SilentMp3();
        Check(ma_decoder_init_memory(mp3.data(), mp3.size(), &config, &decoder) == MA_SUCCESS, "synthetic MP3 decoder init");
        std::vector<float> compressedPcm(4096);
        read = 0;
        Check(ma_decoder_read_pcm_frames(&decoder, compressedPcm.data(), 2048, &read) == MA_SUCCESS && read == 2048, "decode synthetic MP3 frames");
        Check(Rms(compressedPcm) < 1e-7, "synthetic MP3 silence PCM");
        ma_decoder_uninit(&decoder);
        const auto flac = ConstantFlac();
        Check(ma_decoder_init_memory(flac.data(), flac.size(), &config, &decoder) == MA_SUCCESS, "synthetic FLAC decoder init");
        compressedPcm.assign(2048, 0); read = 0;
        Check(ma_decoder_read_pcm_frames(&decoder, compressedPcm.data(), 1024, &read) == MA_SUCCESS && read == 1024, "decode synthetic FLAC frame");
        Check(std::abs(Rms(compressedPcm) - .125) < 1e-6, "lossless FLAC constant PCM");
        ma_decoder_uninit(&decoder);

        mdamp::Equalizer eq;
        auto processed = pcm;
        std::array<float, 10> gains{};
        eq.Configure(false, gains, 10); eq.Process(processed.data(), 48000);
        Check(processed == pcm, "disabled EQ must bypass including preamp");
        eq.Configure(true, gains, 0); eq.Process(processed.data(), 48000);
        Check(std::abs(Rms(processed) - Rms(pcm)) < 1e-6, "flat EQ unity");
        gains[4] = 6; processed = pcm; eq.Configure(true, gains, 0); eq.Process(processed.data(), 48000);
        const double boost = Rms(processed, 12000) / Rms(pcm, 12000);
        Check(boost > 1.98 && boost < 2.01, "1000 Hz +6 dB filter must double amplitude");
        gains[4] = -6; processed = pcm; eq.Configure(true, gains, 0); eq.Process(processed.data(), 48000);
        const double cut = Rms(processed, 12000) / Rms(pcm, 12000);
        Check(cut > .49 && cut < .51, "1000 Hz -6 dB filter must halve amplitude");
        gains = {}; processed = pcm; eq.Configure(true, gains, -6); eq.Process(processed.data(), 48000);
        Check(std::abs(Rms(processed) / Rms(pcm) - .501187) < .0001, "preamp changes PCM");
        processed.assign(96000, 0); processed[0] = .1f; gains.fill(20); eq.Configure(true, gains, 0); eq.Process(processed.data(), 48000);
        for (std::size_t i = 0; i < processed.size(); ++i) Check(std::isfinite(processed[i]) && (i % 2 == 0 || processed[i] == 0), "DSP stable and channel state isolated");
        std::cout << "Audio WAV/MP3/FLAC offline decode, seek, DSP bypass/unity/boost/cut/preamp/stability and engine failure tests passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Audio test failed: " << error.what() << '\n'; return 1; }
}
