#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace mdamp {
struct Track {
    std::filesystem::path path;
    std::wstring title;
};
struct Settings {
    float volume = .75f, pan = 0, preamp = 0;
    float stereoWidth = 0;
    std::array<float, 10> eq{};
    bool eqEnabled = false, shuffle = false, alwaysOnTop = false;
    int repeat = 0, theme = 0, scale = 2;
    bool drawer = true, playlistVisible = true, libraryVisible = false;
    int x = 80, y = 80;
};
std::filesystem::path DefaultDataDirectory();
Settings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const Settings& settings,
                  std::wstring& error);
std::vector<Track> LoadPlaylist(const std::filesystem::path& path, std::wstring& error);
bool SavePlaylist(const std::filesystem::path& path, const std::vector<Track>& tracks,
                  std::wstring& error);
Track MakeTrack(const std::filesystem::path& path);
std::vector<Track> ScanAudioFolder(const std::filesystem::path& path);
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
} // namespace mdamp
