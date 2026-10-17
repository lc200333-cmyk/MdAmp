#include "storage.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace mdamp {
namespace {
constexpr std::size_t kMaxTracks = 10000;
constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
std::atomic<unsigned long> sequence{0};

std::wstring Environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring result(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, result.data(), size);
    if (copied == 0 || copied >= size) return {};
    result.resize(copied);
    return result;
}

float Limit(float value, float low, float high, float fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
Settings Sanitize(Settings value) {
    value.volume = Limit(value.volume, 0, 1, .75f);
    value.pan = Limit(value.pan, -1, 1, 0);
    value.preamp = Limit(value.preamp, -12, 12, 0);
    value.stereoWidth = Limit(value.stereoWidth, 0, 1, 0);
    for (auto& band : value.eq) band = Limit(band, -12, 12, 0);
    value.repeat = std::clamp(value.repeat, -1, 1);
    value.theme = std::clamp(value.theme, 0, 5);
    value.scale = std::clamp(value.scale, 1, 4);
    value.playerWidth = std::clamp(value.playerWidth, 354, 2048);
    value.playlistWidth = std::clamp(value.playlistWidth, 354, 2048);
    value.playlistHeight = std::clamp(value.playlistHeight, 164, 2048);
    value.x = std::clamp(value.x, -32000, 32000);
    value.y = std::clamp(value.y, -32000, 32000);
    return value;
}

bool Read(const std::filesystem::path& path, std::string& data, std::wstring& error) {
    error.clear();
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxFileBytes) {
        error = ec ? L"Cannot read file." : L"File exceeds the 16 MiB safety limit.";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = L"Cannot open file."; return false; }
    data.resize(static_cast<std::size_t>(size));
    if (!data.empty()) file.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file) { error = L"Cannot read complete file."; return false; }
    if (data.size() >= 3 && data.compare(0, 3, "\xef\xbb\xbf") == 0) data.erase(0, 3);
    return true;
}

bool AtomicWrite(const std::filesystem::path& path, const std::string& data,
                 std::wstring& error) {
    error.clear();
    if (path.empty() || path.filename().empty()) {
        error = L"The destination filename is empty.";
        return false;
    }
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = L"Cannot create destination folder."; return false; }
    // CREATE_NEW guarantees that no existing sibling can be overwritten by a temporary write.
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 32 && handle == INVALID_HANDLE_VALUE; ++attempt) {
        temporary = path;
        temporary += L".mdamp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(sequence.fetch_add(1)) + L".tmp";
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE) { error = L"Cannot create temporary file."; return false; }
    DWORD written = 0;
    const bool sizeValid = data.size() <= (std::numeric_limits<DWORD>::max)();
    bool ok = sizeValid && WriteFile(handle, data.data(), static_cast<DWORD>(data.size()),
                                    &written, nullptr) != FALSE && written == data.size();
    if (ok) ok = FlushFileBuffers(handle) != FALSE;
    if (CloseHandle(handle) == FALSE) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) {
        DeleteFileW(temporary.c_str()); // Only the file created above is removed.
        error = L"Cannot save file atomically; the previous destination was preserved.";
    }
    return ok;
}

template <typename T> bool ParseNumber(const std::string& text, T& result) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    T value{};
    input >> value;
    if (!input) return false;
    input >> std::ws;
    if (!input.eof()) return false;
    result = value;
    return true;
}

std::string Trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}
std::wstring OneLine(std::wstring text) {
    for (auto& c : text) if (c == L'\r' || c == L'\n' || c == L'\0') c = L' ';
    return text;
}
bool AudioExtension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension == L".wav" || extension == L".mp3" || extension == L".flac";
}
} // namespace

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length,
                            result.data(), size) != size) return {};
    return result;
}
std::string WideToUtf8(const std::wstring& text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                            result.data(), size, nullptr, nullptr) != size) return {};
    return result;
}
std::filesystem::path DefaultDataDirectory() {
    auto local = Environment(L"LOCALAPPDATA");
    if (!local.empty()) return std::filesystem::path(local) / L"MdAmp";
    const auto profile = Environment(L"USERPROFILE");
    if (!profile.empty()) return std::filesystem::path(profile) / L"AppData" / L"Local" / L"MdAmp";
    return {}; // Never silently write application settings into the current working folder.
}

Settings LoadSettings(const std::filesystem::path& path) {
    Settings settings;
    std::string data;
    std::wstring ignored;
    if (!Read(path, data, ignored)) return settings;
    std::istringstream input(data);
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > 65536) continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        if (key == "volume") ParseNumber(value, settings.volume);
        else if (key == "pan") ParseNumber(value, settings.pan);
        else if (key == "preamp") ParseNumber(value, settings.preamp);
        else if (key == "stereoWidth") ParseNumber(value, settings.stereoWidth);
        else if (key == "repeat") ParseNumber(value, settings.repeat);
        else if (key == "theme") ParseNumber(value, settings.theme);
        else if (key == "scale") ParseNumber(value, settings.scale);
        else if (key == "playerWidth") ParseNumber(value, settings.playerWidth);
        else if (key == "playlistWidth") ParseNumber(value, settings.playlistWidth);
        else if (key == "playlistHeight") ParseNumber(value, settings.playlistHeight);
        else if (key == "x") ParseNumber(value, settings.x);
        else if (key == "y") ParseNumber(value, settings.y);
        else {
            int flag = 0;
            if (key == "eqEnabled" && ParseNumber(value, flag)) settings.eqEnabled = flag != 0;
            else if (key == "shuffle" && ParseNumber(value, flag)) settings.shuffle = flag != 0;
            else if (key == "alwaysOnTop" && ParseNumber(value, flag)) settings.alwaysOnTop = flag != 0;
            else if (key == "drawer" && ParseNumber(value, flag)) settings.drawer = flag != 0;
            else if (key == "playlistVisible" && ParseNumber(value, flag)) settings.playlistVisible = flag != 0;
            else if (key == "playlistDocked" && ParseNumber(value, flag)) settings.playlistDocked = flag != 0;
            else if (key == "libraryVisible" && ParseNumber(value, flag)) settings.libraryVisible = flag != 0;
            else if (key.size() == 3 && key[0] == 'e' && key[1] == 'q' && key[2] >= '0' && key[2] <= '9')
                ParseNumber(value, settings.eq[static_cast<std::size_t>(key[2] - '0')]);
        }
    }
    return Sanitize(settings);
}
bool SaveSettings(const std::filesystem::path& path, const Settings& settings,
                  std::wstring& error) {
    const auto s = Sanitize(settings);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);
    out << "# MdAmp settings v1\nvolume=" << s.volume << "\npan=" << s.pan
        << "\npreamp=" << s.preamp << "\nstereoWidth=" << s.stereoWidth << "\neqEnabled=" << s.eqEnabled
        << "\nshuffle=" << s.shuffle << "\nalwaysOnTop=" << s.alwaysOnTop
        << "\nrepeat=" << s.repeat << "\ntheme=" << s.theme << "\nscale=" << s.scale
        << "\nplayerWidth=" << s.playerWidth << "\nplaylistWidth=" << s.playlistWidth
        << "\nplaylistHeight=" << s.playlistHeight
        << "\ndrawer=" << s.drawer << "\nplaylistVisible=" << s.playlistVisible
        << "\nplaylistDocked=" << s.playlistDocked
        << "\nlibraryVisible=" << s.libraryVisible << "\nx=" << s.x << "\ny=" << s.y << '\n';
    for (std::size_t i = 0; i < s.eq.size(); ++i) out << "eq" << i << '=' << s.eq[i] << '\n';
    return AtomicWrite(path, out.str(), error);
}
Track MakeTrack(const std::filesystem::path& path) {
    return {path.lexically_normal(), path.stem().wstring()};
}
std::vector<Track> LoadPlaylist(const std::filesystem::path& path, std::wstring& error) {
    std::string data;
    if (!Read(path, data, error)) return {};
    if (data.find('\0') != std::string::npos || (!data.empty() && Utf8ToWide(data).empty())) {
        error = L"Playlist must contain valid UTF-8 text.";
        return {};
    }
    std::vector<Track> result;
    std::string line;
    std::wstring title;
    int durationSeconds = -1;
    std::istringstream input(data);
    std::error_code ec;
    const auto absolutePlaylist = std::filesystem::absolute(path, ec);
    if (ec) { error = L"Cannot resolve the playlist directory."; return {}; }
    const auto directory = absolutePlaylist.parent_path();
    while (std::getline(input, line)) {
        if (line.size() > 65536) { title.clear(); durationSeconds = -1; continue; }
        line = Trim(line);
        if (line.empty()) continue;
        if (line.rfind("#EXTINF:", 0) == 0) {
            const auto comma = line.find(',');
            title = comma == std::string::npos ? L"" : Utf8ToWide(line.substr(comma + 1));
            durationSeconds = -1;
            int parsed = -1;
            if (comma != std::string::npos && ParseNumber(line.substr(8, comma - 8), parsed) && parsed >= -1)
                durationSeconds = parsed;
            continue;
        }
        if (line[0] == '#') continue;
        // This player only handles local files. Do not turn URLs into misleading local paths.
        if (line.find("://") != std::string::npos) { title.clear(); durationSeconds = -1; continue; }
        std::filesystem::path media(Utf8ToWide(line));
        if (media.is_relative()) media = directory / media;
        auto track = MakeTrack(media);
        if (!title.empty()) track.title = title;
        track.durationSeconds = durationSeconds;
        title.clear();
        durationSeconds = -1;
        if (result.size() >= kMaxTracks) { error = L"Playlist was limited to 10000 tracks."; break; }
        result.push_back(std::move(track));
    }
    return result;
}
bool SavePlaylist(const std::filesystem::path& path, const std::vector<Track>& tracks,
                  std::wstring& error) {
    if (tracks.size() > kMaxTracks) { error = L"Playlist exceeds 10000 tracks."; return false; }
    std::string data = "\xef\xbb\xbf#EXTM3U\n";
    for (const auto& track : tracks) {
        if (track.path.empty()) continue;
        std::error_code ec;
        const auto absolutePath = std::filesystem::absolute(track.path, ec);
        if (ec) { error = L"Cannot resolve a media path."; return false; }
        const auto absolute = absolutePath.lexically_normal().wstring();
        if (absolute.find_first_of(L"\r\n") != std::wstring::npos || absolute.find(L'\0') != std::wstring::npos) {
            error = L"A media path contains an invalid line break or null character.";
            return false;
        }
        const auto encodedPath = WideToUtf8(absolute);
        const auto title = OneLine(track.title);
        const auto encodedTitle = WideToUtf8(title);
        if (encodedPath.empty() || (!title.empty() && encodedTitle.empty())) {
            error = L"A playlist entry is not valid Unicode.";
            return false;
        }
        data += "#EXTINF:" + std::to_string((std::max)(-1, track.durationSeconds)) + "," +
                encodedTitle + "\n" + encodedPath + "\n";
        if (data.size() > kMaxFileBytes) { error = L"Playlist exceeds the 16 MiB safety limit."; return false; }
    }
    return AtomicWrite(path, data, error);
}
std::vector<Track> ScanAudioFolder(const std::filesystem::path& path) {
    std::vector<Track> result;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    std::size_t visited = 0;
    while (!ec && it != end && result.size() < kMaxTracks && visited++ < 200000) {
        const auto status = it->symlink_status(ec);
        if (ec) break;
        if (std::filesystem::is_symlink(status) || it.depth() >= 64) it.disable_recursion_pending();
        if (!std::filesystem::is_symlink(status) && std::filesystem::is_regular_file(status) && AudioExtension(it->path()))
            result.push_back(MakeTrack(it->path()));
        it.increment(ec);
    }
    std::sort(result.begin(), result.end(), [](const Track& a, const Track& b) { return a.path < b.path; });
    return result;
}
} // namespace mdamp
