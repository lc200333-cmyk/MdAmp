#include "storage.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
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
        mdamp::Settings settings;
        settings.volume = .423f; settings.pan = -.5f; settings.preamp = 6.5f;
        settings.stereoWidth = .625f;
        settings.eq[3] = -7.25f; settings.eqEnabled = true; settings.shuffle = true;
        settings.alwaysOnTop = true; settings.repeat = -1; settings.theme = 2;
        settings.scale = 3; settings.drawer = false; settings.playlistVisible = false;
        settings.libraryVisible = true; settings.x = -200; settings.y = 310;
        Check(mdamp::SaveSettings(settingsFile, settings, error) && error.empty(), "save Unicode-path settings");
        auto loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == settings.volume && loaded.pan == -.5f && loaded.preamp == 6.5f && loaded.stereoWidth == .625f &&
              loaded.eq[3] == -7.25f && loaded.eqEnabled && loaded.shuffle && loaded.alwaysOnTop &&
              loaded.repeat == -1 && loaded.theme == 2 && loaded.scale == 3 && !loaded.drawer &&
              !loaded.playlistVisible && loaded.libraryVisible && loaded.x == -200 && loaded.y == 310,
              "settings full roundtrip");
        settings.volume = std::numeric_limits<float>::quiet_NaN();
        settings.pan = 50; settings.preamp = -99; settings.eq[0] = std::numeric_limits<float>::infinity();
        settings.stereoWidth = 50;
        settings.eq[1] = 99; settings.repeat = 100; settings.scale = -5; settings.x = INT_MAX;
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save clamped settings");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == .75f && loaded.pan == 1 && loaded.preamp == -12 && loaded.stereoWidth == 1 && loaded.eq[0] == 0 &&
              loaded.eq[1] == 12 && loaded.repeat == 1 && loaded.scale == 1 && loaded.x == 32000,
              "nonfinite and range clamps");
        settings.stereoWidth = std::numeric_limits<float>::quiet_NaN();
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save nonfinite stereo width");
        Check(mdamp::LoadSettings(settingsFile).stereoWidth == 0, "nonfinite stereo width defaults");
        settings.stereoWidth = -1;
        Check(mdamp::SaveSettings(settingsFile, settings, error), "save negative stereo width");
        Check(mdamp::LoadSettings(settingsFile).stereoWidth == 0, "negative stereo width clamps");
        Fixture(settingsFile, "volume=not-a-number\npan=0.25garbage\nx=999999999999999999999\n"
                              "eq3=-999\nscale=20\nunknown=anything\nshuffle=true\n");
        loaded = mdamp::LoadSettings(settingsFile);
        Check(loaded.volume == .75f && loaded.pan == 0 && loaded.x == 80 && loaded.eq[3] == -12 &&
              loaded.scale == 4 && !loaded.shuffle, "malformed settings tolerated with safe defaults");

        const auto folder = temp.path / L"Музыка";
        std::filesystem::create_directories(folder / L"nested");
        Fixture(folder / L"Песня.MP3", "synthetic");
        Fixture(folder / L"nested" / L"音楽.flac", "synthetic");
        Fixture(folder / L"tone.wav", "synthetic");
        Fixture(folder / L"ignore.txt", "synthetic");
        auto tracks = mdamp::ScanAudioFolder(folder);
        Check(tracks.size() == 3, "bounded recursive audio scan and case insensitive extensions");
        tracks[0].title = text;
        const auto playlist = temp.path / L"плейлист.m3u8";
        Check(mdamp::SavePlaylist(playlist, tracks, error), "save Unicode M3U8");
        auto restored = mdamp::LoadPlaylist(playlist, error);
        Check(error.empty() && restored.size() == tracks.size() && restored[0].title == text &&
              restored[0].path == tracks[0].path, "playlist path and EXTINF title roundtrip");
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
