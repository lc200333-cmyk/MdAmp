#include "storage.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value, const char* description) {
    if (!value) throw std::runtime_error(description);
}
void Fixture(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    Check(static_cast<bool>(out), "fixture write");
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto root = std::filesystem::temp_directory_path();
        for (unsigned i = 0; i < 1000; ++i) {
            path = root / (L"MdAmp-storage-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(i));
            std::error_code ec;
            if (std::filesystem::create_directory(path, ec)) return;
        }
        throw std::runtime_error("Cannot create isolated temporary folder");
    }
    ~TemporaryDirectory() {
        // path was created with create_directory; no existing directory is reused.
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
}
int main() {
    try {
        TemporaryDirectory temp;
        std::wstring error;
        const std::wstring text = L"Музыка 日本語 🎵";
        Check(mdamp::Utf8ToWide(mdamp::WideToUtf8(text)) == text, "UTF-8 Unicode roundtrip");
        Check(mdamp::Utf8ToWide("\xff").empty(), "invalid UTF-8 rejected");
        Check(mdamp::WideToUtf8(std::wstring(1, static_cast<wchar_t>(0xd800))).empty(), "invalid UTF-16 rejected");

        const auto settingsFile = temp.path / L"настройки" / L"settings.ini";
        auto defaults = mdamp::LoadSettings(settingsFile);
        Check(defaults.volume == .75f && defaults.drawer && defaults.playlistVisible, "missing settings defaults");
        Check(defaults.scale == 1 && defaults.playerWidth == 487 && defaults.playlistWidth == 487 &&
              defaults.playlistHeight == 976 && defaults.playlistDocked, "new installation Modern dimensions and scale");
        mdamp::Settings settings;
        settings.volume = .423f; settings.pan = -.5f; settings.preamp = 6.5f;
        settings.stereoWidth = .625f;
        settings.eq[3] = -7.25f; settings.eqEnabled = true; settings.shuffle = true;
        settings.alwaysOnTop = true; settings.repeat = -1; settings.theme = 2;
        settings.scale = 3; settings.drawer = false; settings.playlistVisible = false;
        settings.playlistDocked = false;
        settings.libraryVisible = true; settings.x = -200; settings.y = 310;
        settings.playerWidth = 640; settings.playlistWidth = 720; settings.playlistHeight = 960;
        Check(mdamp::SaveSettings(settingsFile, settings, error) && error.empty(), "save Unicode-path settings");
        auto loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == settings.volume && loaded.pan == -.5f && loaded.preamp == 6.5f && loaded.stereoWidth == .625f &&
              loaded.eq[3] == -7.25f && loaded.eqEnabled && loaded.shuffle && loaded.alwaysOnTop &&
              loaded.repeat == -1 && loaded.theme == 2 && loaded.scale == 3 && !loaded.drawer &&
              !loaded.playlistVisible && !loaded.playlistDocked && loaded.libraryVisible && loaded.x == -200 && loaded.y == 310 &&
              loaded.playerWidth == 640 && loaded.playlistWidth == 720 && loaded.playlistHeight == 960,
              "settings full roundtrip");
        settings.volume = std::numeric_limits<float>::quiet_NaN();
        settings.pan = 50; settings.preamp = -99; settings.eq[0] = std::numeric_limits<float>::infinity();
        settings.stereoWidth = 50;
        settings.eq[1] = 99; settings.repeat = 100; settings.scale = -5; settings.x = INT_MAX;
        settings.playerWidth = INT_MIN; settings.playlistWidth = INT_MAX; settings.playlistHeight = -1;
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save clamped settings");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == .75f && loaded.pan == 1 && loaded.preamp == -12 && loaded.stereoWidth == 1 && loaded.eq[0] == 0 &&
              loaded.eq[1] == 12 && loaded.repeat == 1 && loaded.scale == 1 && loaded.x == 32000 &&
              loaded.playerWidth == 354 && loaded.playlistWidth == 2048 && loaded.playlistHeight == 164,
              "nonfinite and range clamps");
        settings.stereoWidth = std::numeric_limits<float>::quiet_NaN();
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save nonfinite stereo width");
        Check(mdamp::LoadSettings(settingsFile).stereoWidth == 0, "nonfinite stereo width defaults");
        settings.stereoWidth = -1;
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save negative stereo width");
        Check(mdamp::LoadSettings(settingsFile).stereoWidth == 0, "negative stereo width clamps");
        Fixture(settingsFile, "volume=not-a-number\npan=0.25garbage\nx=999999999999999999999\n"
                              "eq3=-999\nscale=20\nunknown=anything\nshuffle=true\n"
                              "playerWidth=999999999999999999999\nplaylistWidth=not-a-number\nplaylistHeight=720garbage\n"
                              "playlistDocked=false\nplaylistDocked=999999999999999999999\n");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == .75f && loaded.pan == 0 && loaded.x == 80 && loaded.eq[3] == -12 &&
              loaded.scale == 4 && !loaded.shuffle && loaded.playerWidth == 487 && loaded.playlistWidth == 487 &&
              loaded.playlistHeight == 976 && loaded.playlistDocked, "malformed settings tolerated with safe defaults");
        Fixture(settingsFile, "scale=2\nvolume=0.5\n");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.scale == 2 && loaded.volume == .5f && loaded.playerWidth == 487 &&
              loaded.playlistHeight == 976 && loaded.playlistDocked, "legacy settings preserve explicit scale");
        Fixture(settingsFile, "playlistDocked=2\n");
        Check(mdamp::LoadSettings(settingsFile).playlistDocked, "nonzero docking flag is true");
        Fixture(settingsFile, "playlistDocked=0\n");
        Check(!mdamp::LoadSettings(settingsFile).playlistDocked, "zero docking flag is false");
        Fixture(settingsFile, "playerWidth=99999\nplaylistWidth=-20\nplaylistHeight=99999\n");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.playerWidth == 2048 && loaded.playlistWidth == 354 && loaded.playlistHeight == 2048,
              "loaded dimensions clamp both upper and lower bounds");

        const auto folder = temp.path / L"Музыка";
        std::filesystem::create_directories(folder / L"nested");
        Fixture(folder / L"Песня.MP3", "synthetic");
        Fixture(folder / L"nested" / L"音楽.flac", "synthetic");
        Fixture(folder / L"tone.wav", "synthetic");
        Fixture(folder / L"ignore.txt", "synthetic");
        auto tracks = mdamp::ScanAudioFolder(folder);
        Check(tracks.size() == 3, "bounded recursive audio scan and case insensitive extensions");
        tracks[0].title = text;
        Check(tracks[0].durationSeconds == -1, "scanned duration remains unknown without decoding");
        tracks[0].durationSeconds = 123;
        tracks[1].durationSeconds = 0;
        const auto playlist = temp.path / L"плейлист.m3u8";
        Check(mdamp::SavePlaylist(playlist, tracks, error), "save Unicode M3U8");
        auto restored = mdamp::LoadPlaylist(playlist, error);
        Check(error.empty() && restored.size() == tracks.size() && restored[0].title == text &&
              restored[0].path == tracks[0].path && restored[0].durationSeconds == 123 &&
              restored[1].durationSeconds == 0 && restored[2].durationSeconds == -1,
              "playlist path, EXTINF title and duration roundtrip");
        tracks[0].title = L"first\r\nsecond";
        Check(mdamp::SavePlaylist(playlist, tracks, error), "sanitize title line breaks");
        restored = mdamp::LoadPlaylist(playlist, error);
        Check(restored[0].title == L"first  second", "title cannot inject playlist lines");
        const std::string relative = "\xef\xbb\xbf#EXTM3U\r\n#EXTINF:-1," + mdamp::WideToUtf8(text) +
            "\r\n" + mdamp::WideToUtf8(L"Музыка/Песня.MP3") + "\r\n#ignored\nhttps://example.invalid/audio\n";
        Fixture(playlist, relative);
        restored = mdamp::LoadPlaylist(playlist, error);
        Check(error.empty() && restored.size() == 1 && restored[0].title == text &&
              restored[0].path == (folder / L"Песня.MP3"), "BOM CRLF relative resolution and URL rejection");
        Fixture(playlist, "#EXTM3U\n#EXTINF:2,known\nknown.wav\nunknown.wav\n"
                          "#EXTINF:0,zero\nzero.wav\n#EXTINF:-1,unknown\nminus-one.wav\n"
                          "#EXTINF:-2,negative\nnegative.wav\n#EXTINF:2147483647,maximum\nmaximum.wav\n"
                          "#EXTINF:2147483648,overflow\noverflow.wav\n#EXTINF:999999999999999999999999,huge\nhuge.wav\n"
                          "#EXTINF:1.5,fraction\nfraction.wav\n#EXTINF:12garbage,junk\njunk.wav\n"
                          "#EXTINF:,empty\nempty.wav\n#EXTINF:4\nno-comma.wav\n"
                          "#EXTINF:17,rejected-url\nhttps://example.invalid/audio\nafter-url.wav\n"
                          "#EXTINF:19,replaced\n#EXTINF:malformed,replacement\nreplacement.wav\n");
        restored = mdamp::LoadPlaylist(playlist, error);
        const int expectedDurations[] = {2, -1, 0, -1, -1, INT_MAX, -1, -1, -1, -1, -1, -1, -1, -1};
        Check(error.empty() && restored.size() == std::size(expectedDurations), "EXTINF duration fixture loads");
        for (std::size_t i = 0; i < restored.size(); ++i)
            Check(restored[i].durationSeconds == expectedDurations[i], "EXTINF integer bounds and per-track reset");
        Check(restored[4].title == L"negative" && restored[9].title == L"junk" &&
              restored[12].title == L"after-url" && restored[13].title == L"replacement",
              "invalid durations preserve valid titles and rejected entries cannot leak metadata");
        Fixture(playlist, "#EXTINF:23,oversized-metadata\n" + std::string(65537, '#') + "\nafter-long-line.wav\n");
        restored = mdamp::LoadPlaylist(playlist, error);
        Check(error.empty() && restored.size() == 1 && restored[0].durationSeconds == -1 &&
              restored[0].title == L"after-long-line", "oversized lines clear pending EXTINF metadata");
        tracks[0].durationSeconds = -99;
        Check(mdamp::SavePlaylist(playlist, tracks, error), "save invalid in-memory negative duration safely");
        restored = mdamp::LoadPlaylist(playlist, error);
        Check(error.empty() && restored[0].durationSeconds == -1 && restored[1].durationSeconds == 0,
              "saved invalid duration becomes unknown while known zero survives");
        Fixture(playlist, "\xff\xfe");
        Check(mdamp::LoadPlaylist(playlist, error).empty() && !error.empty(), "invalid playlist UTF-8 error");
        Check(mdamp::LoadPlaylist(temp.path / L"missing.m3u8", error).empty() && !error.empty(), "missing playlist error");
        Check(mdamp::ScanAudioFolder(temp.path / L"missing").empty(), "missing scan folder");
        Fixture(playlist, "#EXTM3U\n");
        Check(mdamp::LoadPlaylist(playlist, error).empty() && error.empty(), "empty playlist valid");
        std::string many = "#EXTM3U\n";
        for (int i = 0; i < 10001; ++i) many += "synthetic.wav\n";
        Fixture(playlist, many);
        Check(mdamp::LoadPlaylist(playlist, error).size() == 10000 && !error.empty(), "load track safety bound");
        const std::vector<mdamp::Track> tooMany(10001, mdamp::MakeTrack(folder / L"tone.wav"));
        Check(!mdamp::SavePlaylist(playlist, tooMany, error) && !error.empty(), "save track safety bound");
        const std::vector<mdamp::Track> atLimit(10000, mdamp::MakeTrack(folder / L"tone.wav"));
        Check(mdamp::SavePlaylist(playlist, atLimit, error), "exact track limit can save");
        Check(mdamp::LoadPlaylist(playlist, error).size() == 10000 && error.empty(), "exact track limit loads without truncation warning");
        Fixture(playlist, "x");
        std::filesystem::resize_file(playlist, 16 * 1024 * 1024 + 1);
        Check(mdamp::LoadPlaylist(playlist, error).empty() && !error.empty(), "input byte safety bound");
        Check(!mdamp::SaveSettings(temp.path, settings, error) && !error.empty(), "directory destination failure");
        Check(!mdamp::SaveSettings({}, settings, error), "empty destination failure");
        // A failing replacement must preserve the previous playlist bytes.
        Fixture(playlist, "preserve me");
        const HANDLE lock = CreateFileW(playlist.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(lock != INVALID_HANDLE_VALUE, "lock fixture");
        const bool savedWhileLocked = mdamp::SavePlaylist(playlist, tracks, error);
        CloseHandle(lock);
        Check(!savedWhileLocked && !error.empty(), "atomic replace failure reported");
        std::ifstream preserved(playlist, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(preserved)), std::istreambuf_iterator<char>());
        Check(content == "preserve me", "previous file survives failed replacement");
        preserved.close();
        for (const auto& entry : std::filesystem::directory_iterator(temp.path))
            Check(entry.path().extension() != L".tmp", "temporary files cleaned");
        std::cout << "storage: all checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "storage: " << error.what() << '\n';
        return 1;
    }
}
