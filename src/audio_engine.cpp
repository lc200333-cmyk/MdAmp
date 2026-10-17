#define MA_NO_FLAC_ENCODING
#define MA_NO_WAV_ENCODING
#define MA_NO_ENCODING
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MINIAUDIO_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
// Upstream 0.11.23 implementation warnings; keep strict warnings for MdAmp code.
#pragma warning(disable: 4245 4456)
#endif
#include "../third_party/miniaudio/miniaudio.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "audio_engine.h"
#include "equalizer.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <mutex>

namespace mdamp {
namespace {
constexpr ma_uint32 SampleRate = 48000;
std::wstring ErrorText(const wchar_t* operation, ma_result result) {
    std::wstring message(operation);
    message += L": ";
    const char* description = ma_result_description(result);
    while (description && *description) message.push_back(static_cast<unsigned char>(*description++));
    message += L" (" + std::to_wstring(result) + L")";
    return message;
}
}
struct AudioEngine::Impl {
    // operation serializes device lifetime. Callback only takes state; device APIs
    // are NEVER called while state is held, avoiding stop/uninit callback deadlocks.
    mutable std::mutex operation;
    mutable std::mutex state;
    ma_decoder decoder{};
    ma_device device{};
    bool decoderReady = false, deviceReady = false;
    bool playing = false, ended = false;
    ma_uint64 cursor = 0, length = 0;
    float volume = .7f, pan = 0;
    Equalizer equalizer;
    std::array<float, 32> spectrum{};
    std::wstring format;
    void (*dsp)(float*, std::size_t, void*) = nullptr;
    void* dspUser = nullptr;

    ~Impl() { Close(); }
    void Close() {
        if (deviceReady) { ma_device_uninit(&device); deviceReady = false; }
        std::lock_guard<std::mutex> lock(state);
        if (decoderReady) { ma_decoder_uninit(&decoder); decoderReady = false; }
        playing = ended = false;
        cursor = length = 0;
        spectrum = {};
        format.clear();
        equalizer.Reset();
    }
    void Analyze(const float* samples, std::size_t frames) {
        // Actual post-DSP PCM, logarithmic Goertzel bins; no synthetic animation.
        const std::size_t count = (std::min)(frames, std::size_t(1024));
        if (!count) return;
        constexpr double pi = 3.14159265358979323846;
        for (std::size_t bin = 0; bin < spectrum.size(); ++bin) {
            const double frequency = 50 * std::pow(18000.0 / 50.0, bin / 31.0);
            const double coefficient = 2 * std::cos(2 * pi * frequency / SampleRate);
            double previous = 0, beforePrevious = 0;
            for (std::size_t n = 0; n < count; ++n) {
                const double window = count > 1 ? .5 - .5 * std::cos(2 * pi * n / (count - 1)) : 1;
                const double value = .5 * (samples[n * 2] + samples[n * 2 + 1]) * window;
                const double current = value + coefficient * previous - beforePrevious;
                beforePrevious = previous;
                previous = current;
            }
            const double power = (std::max)(0.0, previous * previous + beforePrevious * beforePrevious - coefficient * previous * beforePrevious);
            const double db = 20 * std::log10((std::max)(1e-7, 4 * std::sqrt(power) / count));
            const float level = static_cast<float>(std::clamp((db + 72) / 72, 0.0, 1.0));
            spectrum[bin] = (std::max)(level, spectrum[bin] * .86f);
        }
    }
    static void Callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto& self = *static_cast<Impl*>(device->pUserData);
        float* samples = static_cast<float*>(output);
        std::fill(samples, samples + std::size_t(frames) * 2, 0.0f);
        std::lock_guard<std::mutex> lock(self.state);
        if (!self.playing || !self.decoderReady) return;
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&self.decoder, samples, frames, &read);
        self.cursor += read;
        self.equalizer.Process(samples, static_cast<std::size_t>(read));
        if (self.dsp) self.dsp(samples, static_cast<std::size_t>(read), self.dspUser);
        const float left = self.volume * (self.pan > 0 ? 1 - self.pan : 1);
        const float right = self.volume * (self.pan < 0 ? 1 + self.pan : 1);
        for (std::size_t frame = 0; frame < static_cast<std::size_t>(read); ++frame) {
            const float l = samples[frame * 2] * left, r = samples[frame * 2 + 1] * right;
            samples[frame * 2] = std::isfinite(l) ? std::clamp(l, -1.0f, 1.0f) : 0;
            samples[frame * 2 + 1] = std::isfinite(r) ? std::clamp(r, -1.0f, 1.0f) : 0;
        }
        self.Analyze(samples, static_cast<std::size_t>(read));
        if (read < frames || result == MA_AT_END || (self.length && self.cursor >= self.length)) {
            self.playing = false;
            self.ended = true;
        }
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;

void AudioEngine::Close() {
    std::lock_guard<std::mutex> operation(impl_->operation);
    impl_->Close();
}

bool AudioEngine::Open(const std::wstring& path, std::wstring& error) {
    auto& self = *impl_;
    std::lock_guard<std::mutex> operation(self.operation);
    self.Close();
    error.clear();
    if (path.empty()) { error = L"No audio file selected."; return false; }
    const auto config = ma_decoder_config_init(ma_format_f32, 2, SampleRate);
    ma_result result;
    {
        std::lock_guard<std::mutex> lock(self.state);
        result = ma_decoder_init_file_w(path.c_str(), &config, &self.decoder);
        if (result == MA_SUCCESS) {
            self.decoderReady = true;
            result = ma_decoder_get_length_in_pcm_frames(&self.decoder, &self.length);
        }
    }
    if (!self.decoderReady) { error = ErrorText(L"Cannot decode file (supported: WAV, MP3, FLAC)", result); return false; }
    if (result != MA_SUCCESS || self.length == 0) {
        error = ErrorText(L"Cannot read a non-empty seekable audio stream", result == MA_SUCCESS ? MA_INVALID_FILE : result);
        self.Close();
        return false;
    }
    auto deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = SampleRate;
    deviceConfig.dataCallback = Impl::Callback;
    deviceConfig.pUserData = &self;
    result = ma_device_init(nullptr, &deviceConfig, &self.device);
    if (result != MA_SUCCESS) {
        error = ErrorText(L"Cannot open Windows WASAPI playback device", result);
        self.Close();
        return false;
    }
    self.deviceReady = true;
    // Device is stopped until Play. Publish final display metadata under state.
    std::lock_guard<std::mutex> lock(self.state);
    const auto dot = path.find_last_of(L'.');
    self.format = dot == std::wstring::npos ? L"Audio" : path.substr(dot + 1);
    std::transform(self.format.begin(), self.format.end(), self.format.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    self.format += L" / 48 kHz / stereo";
    return true;
}

bool AudioEngine::Play() {
    auto& self = *impl_;
    std::lock_guard<std::mutex> operation(self.operation);
    {
        std::lock_guard<std::mutex> lock(self.state);
        if (!self.decoderReady || !self.deviceReady) return false;
        if (self.ended) {
            if (ma_decoder_seek_to_pcm_frame(&self.decoder, 0) != MA_SUCCESS) return false;
            self.cursor = 0;
            self.equalizer.Reset();
        }
        self.ended = false;
        self.playing = true; // Set BEFORE start: a synchronous first callback can read PCM.
    }
    if (ma_device_start(&self.device) != MA_SUCCESS) {
        std::lock_guard<std::mutex> lock(self.state);
        self.playing = false;
        return false;
    }
    return true;
}

void AudioEngine::Pause() {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->playing = false;
    impl_->spectrum = {};
}
void AudioEngine::Stop() {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->playing = impl_->ended = false;
    if (impl_->decoderReady && ma_decoder_seek_to_pcm_frame(&impl_->decoder, 0) == MA_SUCCESS) impl_->cursor = 0;
    impl_->equalizer.Reset();
    impl_->spectrum = {};
}
void AudioEngine::Seek(double seconds) {
    if (!std::isfinite(seconds)) return;
    std::lock_guard<std::mutex> lock(impl_->state);
    if (!impl_->decoderReady) return;
    const double limit = static_cast<double>(impl_->length) / SampleRate;
    const auto frame = static_cast<ma_uint64>(std::clamp(seconds, 0.0, limit) * SampleRate);
    if (ma_decoder_seek_to_pcm_frame(&impl_->decoder, frame) == MA_SUCCESS) {
        impl_->cursor = frame;
        impl_->ended = false;
        impl_->equalizer.Reset();
        impl_->spectrum = {};
    }
}
void AudioEngine::SetVolume(float volume) {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->volume = std::clamp(std::isfinite(volume) ? volume : 0.0f, 0.0f, 1.0f);
}
void AudioEngine::SetPan(float pan) {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->pan = std::clamp(std::isfinite(pan) ? pan : 0.0f, -1.0f, 1.0f);
}
void AudioEngine::SetEqualizer(bool enabled, const std::array<float, 10>& gainsDb, float preampDb) {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->equalizer.Configure(enabled, gainsDb, preampDb);
}
void AudioEngine::SetDspCallback(void (*callback)(float*, std::size_t, void*), void* user) {
    std::lock_guard<std::mutex> lock(impl_->state);
    impl_->dsp = callback;
    impl_->dspUser = user;
}
AudioSnapshot AudioEngine::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->state);
    return {static_cast<double>(impl_->cursor) / SampleRate, static_cast<double>(impl_->length) / SampleRate,
        impl_->playing, impl_->ended, impl_->spectrum, impl_->format};
}
}
