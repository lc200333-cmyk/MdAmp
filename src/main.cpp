#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "audio_engine.h"
#include "modern_ui.h"
#include "playlist_ui.h"
#include "storage.h"
#include "presentation.h"
#include "plugin_api.h"
#include "mdamp_version.h"

namespace {
enum Command : int {
    Open = 100, Add, Folder, LoadList, SaveList, Exit, Play, Pause, Stop, Prev, Next,
    Preferences, PlaylistWindow, LibraryWindow, EqualizerWindow, VisualizationWindow,
    Shuffle, Repeat, AlwaysOnTop, Shade, ScaleOne, ScaleTwo, About, Remove, Clear,
    SelectAll, SortTitle, Reverse, Randomize, Jump, EqFlat, EqRock, EqPop, EqClassical,
    EqDance, PluginConfig, FileInfo, PlaySelection, SelectNone, InvertSelection, ThemeStart = 300,
    Query = 500, TrackList, LibraryList, Tree, PrefApply, PrefClose,
    PrefTop, PrefTheme, PrefScale, PrefVolume, PrefPan, PrefEq, PrefPreamp,
    PrefStereo, PrefRepeat, PrefShuffle, PrefDrawer, PrefBand0 = 600
};
constexpr wchar_t kPlayerClass[] = L"MdAmp.Player", kPlaylistClass[] = L"MdAmp.Playlist";
constexpr wchar_t kLibraryClass[] = L"MdAmp.Library", kVisClass[] = L"MdAmp.Spectrum", kPrefsClass[] = L"MdAmp.Preferences";
struct PluginHost {
    HMODULE module = nullptr;
    const MdAmpDspV1* api = nullptr;
    ~PluginHost() { if (module) FreeLibrary(module); }
    bool Load() {
        wchar_t filename[32768]{};
        const auto n = GetModuleFileNameW(nullptr, filename, 32768);
        if (!n || n == 32768) return false;
        const auto path = std::filesystem::path(filename).parent_path() / L"plugins" / L"MdAmpStereo.dll";
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) return false;
        const auto entry = reinterpret_cast<MdAmpGetDspV1>(GetProcAddress(module, "MdAmpGetPluginV1"));
        if (!entry) return false;
        api = entry();
        if (!api || api->abiVersion != 1 || !api->process || !api->setAmount) { api = nullptr; return false; }
        return true;
    }
    static void Process(float* samples, std::size_t frames, void* user) {
        const auto* plugin = static_cast<PluginHost*>(user);
        if (plugin->api) plugin->api->process(samples, static_cast<std::uint32_t>(frames), 2);
    }
};
struct Application {
    HINSTANCE instance = nullptr;
    HWND player = nullptr, playlist = nullptr, library = nullptr, visualization = nullptr, prefs = nullptr;
    HWND list = nullptr, query = nullptr, libraryList = nullptr, prefTree = nullptr;
    HFONT font = nullptr, playlistFont = nullptr, queryFont = nullptr;
    HBRUSH playlistBrush = nullptr;
    mdamp::Settings settings;
    mdamp::PlayerView view;
    std::filesystem::path dataDirectory;
    std::filesystem::path sessionFile;
    std::vector<mdamp::Track> tracks, libraryTracks;
    std::vector<int> visibleTracks;
    std::vector<HWND> prefControls;
    int current = -1, prefPage = 0;
    bool smoke = false, shuttingDown = false, paused = false, moving = false;
    bool attachedPlaylist = true, attachedLibrary = true, draggingSlider = false, playlistShade = false;
    float stereoWidth = 0;
    mdamp::HitRegion dragRegion{};
    PluginHost plugin; // Reverse destruction order: engine is destroyed before DLL.
    mdamp::AudioEngine audio;
    std::mt19937 random{std::random_device{}()};
    std::wstring status = L"Ready";
    ~Application() {
        if (font) DeleteObject(font);
        if (playlistFont) DeleteObject(playlistFont);
        if (queryFont) DeleteObject(queryFont);
        if (playlistBrush) DeleteObject(playlistBrush);
    }
};
Application* app = nullptr;
LRESULT CALLBACK PlayerProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK AuxiliaryProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreferencesProcedure(HWND, UINT, WPARAM, LPARAM);
void Dispatch(int);
void RefreshPlaylist();
void RefreshPreferences();
void PositionWindows();
void LayoutAuxiliary(HWND);
int S(int value) { return value * app->settings.scale; }
int PlayerWidth() { return mdamp::PlayerLogicalWidth(app->settings.playerWidth); }
int PlayerHeight() { return mdamp::PlayerLogicalHeight(app->view.shade, app->settings.drawer); }
void UpdatePlaylistFonts() {
    const auto makeFont = [](int height) {
        return CreateFontW(-S(height),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
    };
    const auto listFont = makeFont(20), queryFont = makeFont(13);
    if (app->list) SendMessageW(app->list, WM_SETFONT, reinterpret_cast<WPARAM>(listFont), TRUE);
    if (app->query) SendMessageW(app->query, WM_SETFONT, reinterpret_cast<WPARAM>(queryFont), TRUE);
    if (app->playlistFont) DeleteObject(app->playlistFont);
    if (app->queryFont) DeleteObject(app->queryFont);
    app->playlistFont = listFont; app->queryFont = queryFont;
}
void Error(const std::wstring& message) {
    app->status = message;
    if (!app->smoke) MessageBoxW(app->prefs ? app->prefs : app->player, message.c_str(), L"MdAmp", MB_OK | MB_ICONWARNING);
}
HWND Control(HWND parent, const wchar_t* type, const wchar_t* caption, int id,
             int x, int y, int width, int height, DWORD style = 0, DWORD ex = 0) {
    const auto window = CreateWindowExW(ex, type, caption, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), app->instance, nullptr);
    if (window) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
    return window;
}
std::wstring Text(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, text.data(), length + 1); text.resize(static_cast<std::size_t>(length)); return text;
}
void Check(HWND window, bool value) { SendMessageW(window, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
bool Checked(HWND window) { return SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED; }
std::wstring Lower(std::wstring value) { if (!value.empty()) CharLowerBuffW(value.data(), static_cast<DWORD>(value.size())); return value; }
void AddColumn(HWND list, int column, const wchar_t* title, int width) {
    LVCOLUMNW data{}; data.mask = LVCF_TEXT | LVCF_WIDTH; data.pszText = const_cast<wchar_t*>(title); data.cx = width;
    ListView_InsertColumn(list, column, &data);
}
void ColorList(HWND list) {
    const auto palette = mdamp::ThemePalette(app->settings.theme);
    const auto background = list == app->list ? mdamp::PlaylistBackground(app->settings.theme) : palette.panel;
    const auto text = list == app->list ? mdamp::PlaylistText(app->settings.theme) : palette.text;
    ListView_SetBkColor(list, background); ListView_SetTextBkColor(list, background); ListView_SetTextColor(list, text);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER); InvalidateRect(list, nullptr, TRUE);
}
std::vector<int> SelectedTracks() {
    std::vector<int> selected; int row = -1;
    while ((row = ListView_GetNextItem(app->list, row, LVNI_SELECTED)) >= 0)
        if (static_cast<std::size_t>(row) < app->visibleTracks.size()) selected.push_back(app->visibleTracks[static_cast<std::size_t>(row)]);
    return selected;
}
void RefreshPlaylist() {
    if (!app->list) return;
    const auto filter = Lower(Text(app->query));
    SendMessageW(app->list, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(app->list); app->visibleTracks.clear();
    for (std::size_t index = 0; index < app->tracks.size(); ++index) {
        const auto& track = app->tracks[index];
        if (!filter.empty() && Lower(track.title + L" " + track.path.wstring()).find(filter) == std::wstring::npos) continue;
        const auto label = std::to_wstring(index + 1) + L". " + track.title;
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(app->visibleTracks.size()); item.pszText = const_cast<wchar_t*>(label.c_str());
        const int row = ListView_InsertItem(app->list, &item);
        const auto duration = track.durationSeconds >= 0 ? mdamp::FormatDuration(static_cast<std::uint64_t>(track.durationSeconds)) : L"";
        ListView_SetItemText(app->list, row, 1, const_cast<wchar_t*>(duration.c_str()));
        app->visibleTracks.push_back(static_cast<int>(index));
        if (static_cast<int>(index) == app->current) ListView_SetItemState(app->list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(app->list, WM_SETREDRAW, TRUE, 0);
    LayoutAuxiliary(app->playlist); // Refit columns after a vertical scrollbar appears/disappears.
    InvalidateRect(app->list, nullptr, TRUE); InvalidateRect(app->playlist, nullptr, FALSE);
}
void ApplyAudioSettings() {
    app->audio.SetVolume(app->settings.volume); app->audio.SetPan(app->settings.pan);
    app->audio.SetEqualizer(app->settings.eqEnabled, app->settings.eq, app->settings.preamp);
    app->settings.stereoWidth = app->stereoWidth;
    if (app->plugin.api) app->plugin.api->setAmount(app->stereoWidth);
}
bool SaveState() {
    if (app->smoke) return true;
    std::wstring error;
    if (!mdamp::SaveSettings(app->dataDirectory / L"settings.ini", app->settings, error)) { Error(error); return false; }
    if (!mdamp::SavePlaylist(app->sessionFile, app->tracks, error)) { Error(error); return false; }
    return true;
}
bool PlayAt(int index, bool showError = true) {
    if (index < 0 || static_cast<std::size_t>(index) >= app->tracks.size()) return false;
    std::wstring error;
    if (!app->audio.Open(app->tracks[static_cast<std::size_t>(index)].path.wstring(), error)) {
        app->paused = false; app->status = error; if (showError) Error(error); return false;
    }
    app->current = index; app->paused = false; ApplyAudioSettings();
    const auto duration = app->audio.Snapshot().duration;
    if (duration >= 0 && duration <= INT_MAX) app->tracks[static_cast<std::size_t>(index)].durationSeconds = static_cast<int>(duration);
    if (!app->audio.Play()) { if (showError) Error(L"Не удалось начать воспроизведение."); return false; }
    app->status = L"Playing";
    SetWindowTextW(app->player, (app->tracks[static_cast<std::size_t>(index)].title + L" — MdAmp").c_str());
    RefreshPlaylist(); return true;
}
void NextTrack(bool automatic, int direction = 1) {
    if (app->tracks.empty()) return;
    if (automatic && app->settings.repeat == -1) { PlayAt(app->current, false); return; }
    int index = app->current + direction;
    if (app->settings.shuffle && app->tracks.size() > 1) {
        std::uniform_int_distribution<int> distribution(0, static_cast<int>(app->tracks.size()) - 2);
        index = distribution(app->random); if (index >= app->current) ++index;
    }
    if (index >= static_cast<int>(app->tracks.size())) {
        if (automatic && app->settings.repeat == 0) { app->audio.Stop(); app->status = L"End of playlist"; return; } index = 0;
    }
    if (index < 0) index = static_cast<int>(app->tracks.size()) - 1;
    PlayAt(index, !automatic);
}
std::vector<std::filesystem::path> PickFiles(bool playlist, bool save) {
    std::vector<wchar_t> buffer(65536, 0); OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = app->prefs ? app->prefs : app->player;
    dialog.lpstrFile = buffer.data(); dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = playlist ? L"M3U playlists\0*.m3u;*.m3u8\0All files\0*.*\0" : L"Audio files\0*.mp3;*.flac;*.wav\0Playlists\0*.m3u;*.m3u8\0All files\0*.*\0";
    dialog.lpstrDefExt = playlist ? L"m3u8" : nullptr;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!playlist && !save) dialog.Flags |= OFN_ALLOWMULTISELECT;
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
    std::vector<std::filesystem::path> files; const std::filesystem::path first(buffer.data());
    const wchar_t* next = buffer.data() + wcslen(buffer.data()) + 1;
    if (!*next || save) files.push_back(first); else for (; *next; next += wcslen(next) + 1) files.push_back(first / next);
    return files;
}
std::filesystem::path PickFolder() {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    DWORD options = 0; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    std::filesystem::path path;
    if (SUCCEEDED(dialog->Show(app->player))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) { PWSTR value = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) { path = value; CoTaskMemFree(value); } item->Release(); }
    }
    dialog->Release(); return path;
}
void AppendFiles(const std::vector<std::filesystem::path>& files, bool replace, bool start) {
    if (files.empty()) return;
    std::vector<mdamp::Track> additions;
    for (const auto& file : files) {
        std::error_code directoryError;
        if (std::filesystem::is_directory(file,directoryError)) { auto scanned = mdamp::ScanAudioFolder(file); additions.insert(additions.end(),scanned.begin(),scanned.end()); continue; }
        const auto extension = Lower(file.extension().wstring());
        if (extension == L".m3u" || extension == L".m3u8") {
            std::wstring error; auto imported = mdamp::LoadPlaylist(file, error); if (!error.empty()) { Error(error); return; }
            additions.insert(additions.end(), imported.begin(), imported.end());
        } else additions.push_back(mdamp::MakeTrack(file));
    }
    if (additions.empty() && !replace) return;
    if ((replace ? 0 : app->tracks.size()) + additions.size() > 10000) { Error(L"Playlist limit: 10,000 tracks. Existing playlist was preserved."); return; }
    if (replace) { app->audio.Close(); app->tracks.clear(); app->current = -1; app->paused = false; }
    const auto first = static_cast<int>(app->tracks.size());
    app->tracks.insert(app->tracks.end(), additions.begin(), additions.end()); RefreshPlaylist(); if (start) PlayAt(first);
}
bool AddTracks(const std::vector<mdamp::Track>& tracks) {
    if (app->tracks.size() + tracks.size() > 10000) { Error(L"Playlist limit: 10,000 tracks. No tracks added."); return false; }
    app->tracks.insert(app->tracks.end(), tracks.begin(), tracks.end()); RefreshPlaylist(); return true;
}
void LoadLibraryFolder() {
    const auto folder = PickFolder(); if (folder.empty()) return;
    app->libraryTracks = mdamp::ScanAudioFolder(folder); ListView_DeleteAllItems(app->libraryList);
    for (std::size_t i = 0; i < app->libraryTracks.size(); ++i) {
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = const_cast<wchar_t*>(app->libraryTracks[i].title.c_str());
        const int row = ListView_InsertItem(app->libraryList, &item); auto path = app->libraryTracks[i].path.wstring(); ListView_SetItemText(app->libraryList, row, 1, path.data());
    }
}
void PositionWindows() {
    if (!app->player || app->moving) return;
    RECT rect{}; GetWindowRect(app->player, &rect); app->moving = true;
    if (app->playlist && app->attachedPlaylist) SetWindowPos(app->playlist, nullptr, rect.left, rect.bottom + S(1),
        rect.right - rect.left, S(app->playlistShade ? 17 : app->settings.playlistHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    int libraryTop = rect.bottom + S(1);
    if (app->settings.playlistVisible && app->attachedPlaylist) libraryTop += S(app->playlistShade ? 17 : app->settings.playlistHeight);
    if (app->library && app->attachedLibrary) SetWindowPos(app->library, nullptr, rect.left, libraryTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    app->moving = false;
}
void ResizePlayer() {
    app->view.drawer = app->settings.drawer;
    const auto style = GetWindowLongPtrW(app->player,GWL_STYLE);
    SetWindowLongPtrW(app->player,GWL_STYLE,app->view.shade ? style & ~static_cast<LONG_PTR>(WS_THICKFRAME) : style | WS_THICKFRAME);
    SetWindowPos(app->player, app->settings.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
        S(PlayerWidth()), S(PlayerHeight()), SWP_NOMOVE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    PositionWindows(); InvalidateRect(app->player, nullptr, FALSE);
}
void ThemeChanged() {
    app->settings.theme = std::clamp(app->settings.theme, 0, static_cast<int>(mdamp::ThemeNames().size()) - 1);
    ColorList(app->list); ColorList(app->libraryList);
    if (app->playlistBrush) DeleteObject(app->playlistBrush);
    app->playlistBrush = CreateSolidBrush(mdamp::PlaylistBackground(app->settings.theme));
    if (app->query) InvalidateRect(app->query,nullptr,TRUE);
    for (const auto window : {app->player, app->playlist, app->library, app->visualization}) if (window) InvalidateRect(window, nullptr, TRUE);
}
void AddMenuItem(HMENU menu, int id, const wchar_t* text, bool checked = false) { AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), static_cast<UINT_PTR>(id), text); }
void Menu(int section, HWND owner, POINT position) {
    const auto menu = CreatePopupMenu();
    if (section == 0 || section == -1) {
        AddMenuItem(menu, Open, L"Play file(s)...\tL / Ctrl+O"); AddMenuItem(menu, Add, L"Add file(s)...\tCtrl+L"); AddMenuItem(menu, Folder, L"Add folder...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AddMenuItem(menu, LoadList, L"Load playlist...\tCtrl+Shift+O"); AddMenuItem(menu, SaveList, L"Save playlist...\tCtrl+S"); AddMenuItem(menu, FileInfo, L"View file info...\tAlt+3");
        if (section == 0) { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AddMenuItem(menu, Exit, L"Exit\tAlt+F4"); }
    }
    if (section == 1 || section == -1) {
        if (section == -1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AddMenuItem(menu, Play, L"Play\tX"); AddMenuItem(menu, Pause, L"Pause\tC / Space"); AddMenuItem(menu, Stop, L"Stop\tV");
        AddMenuItem(menu, Prev, L"Previous\tZ"); AddMenuItem(menu, Next, L"Next\tB"); AddMenuItem(menu, Jump, L"Jump to file...\tJ");
        AddMenuItem(menu, Shuffle, L"Shuffle\tS", app->settings.shuffle);
        AddMenuItem(menu, Repeat, app->settings.repeat == -1 ? L"Repeat: track\tR" : app->settings.repeat == 1 ? L"Repeat: playlist\tR" : L"Repeat: off\tR", app->settings.repeat != 0);
    }
    if (section == 2 || section == -1) {
        if (section == -1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AddMenuItem(menu, Preferences, L"Preferences...\tCtrl+P"); AddMenuItem(menu, AlwaysOnTop, L"Always on top\tCtrl+T", app->settings.alwaysOnTop);
        AddMenuItem(menu, Shade, L"Windowshade\tCtrl+W", app->view.shade); AddMenuItem(menu, ScaleOne, L"100% scale", app->settings.scale == 1); AddMenuItem(menu, ScaleTwo, L"200% scale", app->settings.scale == 2);
        const auto themes = CreatePopupMenu(); const auto names = mdamp::ThemeNames();
        for (std::size_t i = 0; i < names.size(); ++i) AddMenuItem(themes, ThemeStart + static_cast<int>(i), names[i].c_str(), app->settings.theme == static_cast<int>(i));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themes), L"Color themes");
    }
    if (section == 3 || section == -1) {
        if (section == -1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AddMenuItem(menu, PlaylistWindow, L"Playlist Editor\tAlt+E", app->settings.playlistVisible);
        AddMenuItem(menu, EqualizerWindow, L"Equalizer\tAlt+G", app->settings.drawer);
        AddMenuItem(menu, LibraryWindow, L"Media Library\tAlt+L", app->settings.libraryVisible);
        AddMenuItem(menu, VisualizationWindow, L"Visualization\tCtrl+Shift+K", IsWindowVisible(app->visualization) != FALSE);
    }
    if (section == 4) { AddMenuItem(menu, About, L"About MdAmp..."); AddMenuItem(menu, Preferences, L"Preferences / Shortcuts..."); }
    const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, owner, nullptr);
    DestroyMenu(menu); if (selected) Dispatch(static_cast<int>(selected));
}
void EqPresets(POINT position) {
    const auto menu = CreatePopupMenu(); AddMenuItem(menu, EqFlat, L"Flat"); AddMenuItem(menu, EqRock, L"Rock"); AddMenuItem(menu, EqPop, L"Pop"); AddMenuItem(menu, EqClassical, L"Classical"); AddMenuItem(menu, EqDance, L"Dance");
    const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, app->player, nullptr); DestroyMenu(menu); if (selected) Dispatch(static_cast<int>(selected));
}
void ShowPreferences() {
    if (app->prefs) { ShowWindow(app->prefs, SW_SHOW); SetForegroundWindow(app->prefs); return; }
    RECT rect{}; GetWindowRect(app->player, &rect);
    app->prefs = CreateWindowExW(WS_EX_DLGMODALFRAME, kPrefsClass, L"MdAmp Preferences", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        rect.left + 30, rect.top + 30, 760, 520, app->player, nullptr, app->instance, nullptr);
    if (app->prefs && !app->smoke) ShowWindow(app->prefs, SW_SHOW);
}
void Dispatch(int command) {
    if (command >= ThemeStart && command < ThemeStart + static_cast<int>(mdamp::ThemeNames().size())) { app->settings.theme = command - ThemeStart; ThemeChanged(); return; }
    switch (command) {
    case Open: AppendFiles(PickFiles(false, false), true, true); break;
    case Add: AppendFiles(PickFiles(false, false), false, false); break;
    case Folder: { const auto folder = PickFolder(); if (!folder.empty()) AddTracks(mdamp::ScanAudioFolder(folder)); break; }
    case LoadList: { const auto files = PickFiles(true, false); if (!files.empty()) AppendFiles(files, true, false); break; }
    case SaveList: { const auto files = PickFiles(true, true); if (!files.empty()) { std::wstring error; if (!mdamp::SavePlaylist(files[0], app->tracks, error)) Error(error); } break; }
    case Exit: SendMessageW(app->player, WM_CLOSE, 0, 0); break;
    case PlaySelection: { const auto selected = SelectedTracks(); if (!selected.empty()) PlayAt(selected.front()); break; }
    case Play:
        if (app->audio.Snapshot().duration > 0 && app->paused) { if (app->audio.Play()) app->paused = false; else Error(L"Cannot resume playback."); }
        else if (app->audio.Snapshot().duration > 0 && app->audio.Snapshot().playing) { const auto selected = SelectedTracks(); if (!selected.empty() && selected.front() != app->current) PlayAt(selected.front()); else app->audio.Seek(0); }
        else { const auto selected = SelectedTracks(); PlayAt(selected.empty() ? std::max(0, app->current) : selected.front()); } break;
    case Pause:
        if (app->audio.Snapshot().playing) { app->audio.Pause(); app->paused = true; app->status = L"Paused"; }
        else if (app->audio.Snapshot().duration > 0) { if (app->audio.Play()) { app->paused = false; app->status = L"Playing"; } else Error(L"Cannot resume playback."); } else Dispatch(Play); break;
    case Stop: app->audio.Stop(); app->paused = false; app->status = L"Stopped"; break;
    case Prev: NextTrack(false, -1); break; case Next: NextTrack(false); break;
    case Preferences: ShowPreferences(); break;
    case PlaylistWindow: app->settings.playlistVisible = !app->settings.playlistVisible; ShowWindow(app->playlist, app->settings.playlistVisible ? SW_SHOWNOACTIVATE : SW_HIDE); PositionWindows(); break;
    case LibraryWindow: app->settings.libraryVisible = !app->settings.libraryVisible; ShowWindow(app->library, app->settings.libraryVisible ? SW_SHOWNOACTIVATE : SW_HIDE); break;
    case EqualizerWindow: app->settings.drawer = !app->settings.drawer; app->view.tab = 0; ResizePlayer(); break;
    case VisualizationWindow: ShowWindow(app->visualization, IsWindowVisible(app->visualization) ? SW_HIDE : SW_SHOWNOACTIVATE); break;
    case Shuffle: app->settings.shuffle = !app->settings.shuffle; break;
    case Repeat: app->settings.repeat = app->settings.repeat == 0 ? 1 : app->settings.repeat == 1 ? -1 : 0; break;
    case AlwaysOnTop: app->settings.alwaysOnTop = !app->settings.alwaysOnTop; ResizePlayer(); break;
    case Shade: app->view.shade = !app->view.shade; ResizePlayer(); break;
    case ScaleOne: case ScaleTwo:
        app->settings.scale = command == ScaleOne ? 1 : 2;
        app->moving = true;
        SetWindowPos(app->playlist,nullptr,0,0,S(app->settings.playlistWidth),S(app->playlistShade ? 17 : app->settings.playlistHeight),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(app->library,nullptr,0,0,S(790),S(260),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(app->visualization,nullptr,0,0,S(354),S(200),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        MoveWindow(GetDlgItem(app->library,Folder),S(8),S(43),S(110),S(22),TRUE);
        MoveWindow(GetDlgItem(app->library,Add),S(126),S(43),S(145),S(22),TRUE);
        UpdatePlaylistFonts(); LayoutAuxiliary(app->playlist);
        ListView_SetColumnWidth(app->libraryList,0,S(280)); ListView_SetColumnWidth(app->libraryList,1,S(440));
        app->moving = false;
        ResizePlayer(); break;
    case About: MessageBoxW(app->player, L"MdAmp " MDAMP_VERSION_W L"\n\nIndependent C++ / Win32 audio player.\nWAV / MP3 / FLAC, WASAPI, 10-band EQ, playlists, spectrum.\nOriginal code and vector UI; miniaudio under its upstream license.\n\nLegacy Winamp DLL / MAKI / AVS / MilkDrop compatibility is not implemented.", L"About MdAmp", MB_OK | MB_ICONINFORMATION); break;
    case Remove: { auto selected = SelectedTracks(); std::sort(selected.rbegin(), selected.rend());
        for (const auto index : selected) { if (index == app->current) { app->audio.Close(); app->paused = false; app->current = -1; } else if (index < app->current) --app->current; app->tracks.erase(app->tracks.begin() + index); } RefreshPlaylist(); break; }
    case Clear: app->audio.Close(); app->paused = false; app->tracks.clear(); app->current = -1; RefreshPlaylist(); break;
    case SelectAll: ListView_SetItemState(app->list, -1, LVIS_SELECTED, LVIS_SELECTED); break;
    case SelectNone: ListView_SetItemState(app->list, -1, 0, LVIS_SELECTED); break;
    case InvertSelection:
        for (int i = 0; i < ListView_GetItemCount(app->list); ++i) {
            const auto selected = ListView_GetItemState(app->list,i,LVIS_SELECTED);
            ListView_SetItemState(app->list,i,selected ? 0 : LVIS_SELECTED,LVIS_SELECTED);
        } break;
    case SortTitle: case Reverse: case Randomize: {
        std::vector<std::pair<mdamp::Track,int>> ordered;
        for (std::size_t i = 0; i < app->tracks.size(); ++i) ordered.emplace_back(app->tracks[i],static_cast<int>(i));
        if (command == SortTitle) std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return Lower(a.first.title) < Lower(b.first.title); });
        else if (command == Reverse) std::reverse(ordered.begin(),ordered.end()); else std::shuffle(ordered.begin(),ordered.end(),app->random);
        const int previous = app->current; app->current = -1;
        for (std::size_t i = 0; i < ordered.size(); ++i) { app->tracks[i] = std::move(ordered[i].first); if (ordered[i].second == previous) app->current = static_cast<int>(i); }
        RefreshPlaylist(); break;
    }
    case Jump: app->settings.playlistVisible = true; ShowWindow(app->playlist, SW_SHOW); SetForegroundWindow(app->playlist); SetFocus(app->query); SendMessageW(app->query, EM_SETSEL, 0, -1); break;
    case EqFlat: case EqRock: case EqPop: case EqClassical: case EqDance: {
        static constexpr std::array<std::array<float, 10>, 5> presets{{{{0,0,0,0,0,0,0,0,0,0}},{{4,3,1,-1,-2,-1,2,4,5,5}},{{-1,1,3,4,3,1,-1,-2,-1,0}},{{3,2,1,0,-1,0,1,2,3,4}},{{5,4,1,0,0,2,3,3,2,1}}}};
        app->settings.eq = presets[static_cast<std::size_t>(command - EqFlat)]; app->settings.preamp = command == EqFlat ? 0.0f : -5.0f; app->settings.eqEnabled = command != EqFlat; ApplyAudioSettings(); break;
    }
    case PluginConfig: ShowPreferences(); app->prefPage = 5; RefreshPreferences(); break;
    case FileInfo: if (app->current >= 0) { const auto audio = app->audio.Snapshot(); const auto message = app->tracks[static_cast<std::size_t>(app->current)].path.wstring() + L"\n\n" + audio.format + L"\nDuration: " + mdamp::FormatDuration(static_cast<std::uint64_t>(audio.duration)); MessageBoxW(app->player, message.c_str(), L"MdAmp — File Info", MB_OK | MB_ICONINFORMATION); } break;
    }
    InvalidateRect(app->player, nullptr, FALSE);
}

void HandleAction(mdamp::Action action) {
    using A = mdamp::Action;
    switch (action) {
    case A::Previous: Dispatch(Prev); break; case A::Play: Dispatch(Play); break; case A::Pause: Dispatch(Pause); break;
    case A::Stop: Dispatch(Stop); break; case A::Next: Dispatch(Next); break; case A::Open: Dispatch(Open); break;
    case A::TogglePlaylist: Dispatch(PlaylistWindow); break; case A::ToggleLibrary: Dispatch(LibraryWindow); break;
    case A::Shuffle: Dispatch(Shuffle); break; case A::Repeat: Dispatch(Repeat); break; case A::Preferences: Dispatch(Preferences); break;
    case A::Minimize: ShowWindow(app->player, SW_MINIMIZE); break; case A::Shade: Dispatch(Shade); break; case A::Close: Dispatch(Exit); break;
    case A::ToggleDrawer: Dispatch(EqualizerWindow); break;
    case A::DrawerEq: case A::DrawerOptions: case A::DrawerThemes:
        app->view.tab = action == A::DrawerEq ? 0 : action == A::DrawerOptions ? 1 : 2; app->settings.drawer = true; ResizePlayer(); break;
    case A::EqToggle: app->settings.eqEnabled = !app->settings.eqEnabled; ApplyAudioSettings(); break;
    case A::EqAuto: Dispatch(EqFlat); break;
    case A::EqPreset: { POINT point{}; GetCursorPos(&point); EqPresets(point); break; }
    case A::ThemePrevious: app->settings.theme = (app->settings.theme + static_cast<int>(mdamp::ThemeNames().size()) - 1) % static_cast<int>(mdamp::ThemeNames().size()); ThemeChanged(); break;
    case A::ThemeNext: app->settings.theme = (app->settings.theme + 1) % static_cast<int>(mdamp::ThemeNames().size()); ThemeChanged(); break;
    case A::ThemeSelect: { POINT point{}; GetCursorPos(&point); Menu(2, app->player, point); break; }
    case A::Visualize: Dispatch(VisualizationWindow); break;
    default: break;
    }
}
void UpdateSlider(POINT point) {
    const auto& region = app->dragRegion;
    const float x = std::clamp(static_cast<float>(point.x - region.rect.left) / std::max(1L, region.rect.right - region.rect.left), 0.0f, 1.0f);
    const float y = std::clamp(static_cast<float>(region.rect.bottom - point.y) / std::max(1L, region.rect.bottom - region.rect.top), 0.0f, 1.0f);
    using A = mdamp::Action;
    if (region.action == A::Seek) app->audio.Seek(app->audio.Snapshot().duration * x);
    else if (region.action == A::Volume) app->settings.volume = x;
    else if (region.action == A::Pan) app->settings.pan = x * 2 - 1;
    else if (region.action == A::Preamp) app->settings.preamp = y * 24 - 12;
    else if (region.action >= A::Band0 && region.action <= A::Band9) app->settings.eq[static_cast<std::size_t>(static_cast<int>(region.action) - static_cast<int>(A::Band0))] = y * 24 - 12;
    ApplyAudioSettings(); InvalidateRect(app->player, nullptr, FALSE);
}
void PaintPlayerWindow(HWND window) {
    PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
    const auto memory = CreateCompatibleDC(dc); const auto bitmap = CreateCompatibleBitmap(dc, std::max(1L, client.right), std::max(1L, client.bottom)); const auto old = SelectObject(memory, bitmap);
    app->view.width = std::max(354, static_cast<int>(client.right) / app->settings.scale);
    SetMapMode(memory, MM_ANISOTROPIC); SetWindowExtEx(memory, 1, 1, nullptr); SetViewportExtEx(memory, app->settings.scale, app->settings.scale, nullptr);
    mdamp::PaintPlayer(memory, app->view); SetMapMode(memory, MM_TEXT);
    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(window, &paint);
}
void UpdateView() {
    const auto audio = app->audio.Snapshot();
    app->view.title = app->current >= 0 && static_cast<std::size_t>(app->current) < app->tracks.size() ?
        app->tracks[static_cast<std::size_t>(app->current)].title + L" (" + mdamp::FormatDuration(static_cast<std::uint64_t>(std::max(0.0,audio.duration))) + L")" : L"MdAmp " MDAMP_VERSION_W;
    app->view.info = audio.duration > 0 ? audio.format : app->status;
    app->view.time = mdamp::FormatDuration(static_cast<std::uint64_t>(std::max(0.0, audio.position)));
    app->view.playing = audio.playing; app->view.paused = app->paused;
    app->view.progress = audio.duration > 0 ? static_cast<float>(audio.position / audio.duration) : 0;
    app->view.volume = app->settings.volume; app->view.pan = app->settings.pan; app->view.eq = app->settings.eq; app->view.preamp = app->settings.preamp;
    app->view.eqEnabled = app->settings.eqEnabled; app->view.shuffle = app->settings.shuffle; app->view.repeat = app->settings.repeat;
    app->view.theme = app->settings.theme; app->view.drawer = app->settings.drawer; app->view.spectrum = audio.spectrum;
    app->view.playlistVisible = app->settings.playlistVisible; app->view.libraryVisible = app->settings.libraryVisible;
    if (audio.ended && !app->paused) NextTrack(true);
    InvalidateRect(app->player, nullptr, FALSE); if (IsWindowVisible(app->visualization)) InvalidateRect(app->visualization, nullptr, FALSE);
    if (IsWindowVisible(app->playlist)) { RECT client{}; GetClientRect(app->playlist, &client); client.top = client.bottom - S(24); InvalidateRect(app->playlist, &client, FALSE); }
}
LRESULT CALLBACK PlayerProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(window,&point);
        RECT bounds{}; GetClientRect(window,&bounds);
        if (point.x < S(3)) return HTLEFT;
        if (point.x >= bounds.right - S(3)) return HTRIGHT;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {S(354),S(PlayerHeight())};
        limits->ptMaxTrackSize = {S(2048),S(PlayerHeight())}; return 0;
    }
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED) { RECT client{}; GetClientRect(window,&client);
            app->settings.playerWidth = std::clamp(static_cast<int>(client.right) / app->settings.scale,354,2048);
            PositionWindows(); InvalidateRect(window,nullptr,FALSE); } return 0;
    }
    case WM_PAINT: PaintPlayerWindow(window); return 0; case WM_ERASEBKGND: return 1;
    case WM_TIMER: UpdateView(); return 0;
    case WM_MOVE: if (!IsIconic(window)) { RECT rect{}; GetWindowRect(window, &rect); app->settings.x = rect.left; app->settings.y = rect.top; PositionWindows(); } return 0;
    case WM_LBUTTONDBLCLK: if (GET_Y_LPARAM(lParam) < S(17) && GET_X_LPARAM(lParam) > S(40) && GET_X_LPARAM(lParam) < S(PlayerWidth() - 44)) Dispatch(Shade); return 0;
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam) / app->settings.scale, GET_Y_LPARAM(lParam) / app->settings.scale};
        const auto regions = mdamp::PlayerHits(app->view.shade, app->settings.drawer, app->view.tab, PlayerWidth());
        for (const auto& region : regions) {
            if (!PtInRect(&region.rect, point)) continue;
            using A = mdamp::Action;
            if (region.action == A::Seek || region.action == A::Volume || region.action == A::Pan || region.action == A::Preamp || (region.action >= A::Band0 && region.action <= A::Band9)) { app->dragRegion = region; app->draggingSlider = true; SetCapture(window); UpdateSlider(point); }
            else HandleAction(region.action); return 0;
        }
        if (!app->view.shade) {
            const auto menus = mdamp::PlayerMenuRects();
            for (std::size_t i = 0; i < menus.size(); ++i) if (PtInRect(&menus[i],point)) {
                POINT screen{S(menus[i].left),S(menus[i].bottom)}; ClientToScreen(window,&screen);
                Menu(static_cast<int>(i),window,screen); return 0;
            }
        }
        if (point.y < 17) SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_MOUSEMOVE: if (app->draggingSlider) UpdateSlider({GET_X_LPARAM(lParam) / app->settings.scale, GET_Y_LPARAM(lParam) / app->settings.scale}); return 0;
    case WM_LBUTTONUP: if (app->draggingSlider) { app->draggingSlider = false; ReleaseCapture(); SaveState(); } return 0;
    case WM_CAPTURECHANGED: app->draggingSlider = false; return 0;
    case WM_CONTEXTMENU: { POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; if (point.x == -1) GetCursorPos(&point); Menu(-1, window, point); return 0; }
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(wParam); const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0); std::vector<std::filesystem::path> files;
        for (UINT i = 0; i < count; ++i) { const UINT length = DragQueryFileW(drop, i, nullptr, 0); std::wstring path(static_cast<std::size_t>(length) + 1, L'\0'); DragQueryFileW(drop, i, path.data(), length + 1); path.resize(length); files.emplace_back(path); }
        DragFinish(drop); AppendFiles(files, false, true); return 0;
    }
    case WM_CLOSE: if (!app->shuttingDown) { if (!SaveState()) return 0; app->shuttingDown = true; app->audio.Close(); } DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void LayoutAuxiliary(HWND window) {
    RECT client{}; GetClientRect(window, &client); const int width = client.right, height = client.bottom;
    if (window == app->playlist) {
        const auto layout = mdamp::ComputePlaylistLayout(width / app->settings.scale,height / app->settings.scale);
        const auto move = [](HWND control, const RECT& rect) {
            if (control) MoveWindow(control,S(rect.left),S(rect.top),S(rect.right-rect.left),S(rect.bottom-rect.top),TRUE);
        };
        move(app->query,layout.query); move(app->list,layout.list);
        RECT listBounds{}; GetClientRect(app->list,&listBounds);
        ListView_SetColumnWidth(app->list,0,std::max(1L,listBounds.right - S(64)));
        ListView_SetColumnWidth(app->list,1,S(64));
    } else if (window == app->library) MoveWindow(app->libraryList, S(8), S(72), std::max(10, width - S(16)), std::max(10, height - S(80)), TRUE);
}
void PlaylistFooterMenu(mdamp::PlaylistFooterAction action, POINT position) {
    const auto menu = CreatePopupMenu();
    using A = mdamp::PlaylistFooterAction;
    switch (action) {
    case A::Add: AddMenuItem(menu,Add,L"Add file(s)...\tCtrl+L"); AddMenuItem(menu,Folder,L"Add folder..."); break;
    case A::Remove: AddMenuItem(menu,Remove,L"Remove selected\tDelete"); AddMenuItem(menu,Clear,L"Clear playlist"); break;
    case A::Select: AddMenuItem(menu,SelectAll,L"Select all\tCtrl+A"); AddMenuItem(menu,SelectNone,L"Select none"); AddMenuItem(menu,InvertSelection,L"Invert selection"); break;
    case A::Misc: AddMenuItem(menu,SortTitle,L"Sort by title"); AddMenuItem(menu,Reverse,L"Reverse order"); AddMenuItem(menu,Randomize,L"Randomize order"); break;
    case A::Manage: AddMenuItem(menu,LoadList,L"Load playlist...\tCtrl+Shift+O"); AddMenuItem(menu,SaveList,L"Save playlist...\tCtrl+S"); break;
    default: DestroyMenu(menu); return;
    }
    const auto selected = TrackPopupMenu(menu,TPM_RETURNCMD | TPM_RIGHTBUTTON,position.x,position.y,0,app->playlist,nullptr);
    DestroyMenu(menu); if (selected) Dispatch(static_cast<int>(selected));
}
LRESULT CALLBACK QueryProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR) {
    const auto result = DefSubclassProc(window,message,wParam,lParam);
    if (message == WM_PAINT && GetWindowTextLengthW(window) == 0 && GetFocus() != window) {
        const auto dc = GetDC(window); RECT bounds{}; GetClientRect(window,&bounds);
        const auto old = SelectObject(dc,app->queryFont); SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(235,242,255));
        DrawTextW(dc,L"Search in Playlist",-1,&bounds,DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc,old); ReleaseDC(window,dc);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window,QueryProcedure,1);
    return result;
}
void DrawPlaylistRow(const DRAWITEMSTRUCT& item) {
    if (item.itemID >= app->visibleTracks.size()) return;
    const int index = app->visibleTracks[item.itemID];
    if (index < 0 || static_cast<std::size_t>(index) >= app->tracks.size()) return;
    const auto& track = app->tracks[static_cast<std::size_t>(index)];
    const auto dc = item.hDC; auto bounds = item.rcItem;
    const bool focusedSelection = (item.itemState & ODS_SELECTED) != 0 && GetFocus() == app->list;
    const auto brush = CreateSolidBrush(focusedSelection ? RGB(48,85,148) : mdamp::PlaylistBackground(app->settings.theme));
    FillRect(dc,&bounds,brush); DeleteObject(brush);
    const auto old = SelectObject(dc,app->playlistFont); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,mdamp::PlaylistText(app->settings.theme));
    auto titleBounds = bounds; titleBounds.right -= S(64); titleBounds.left += S(1);
    const auto title = std::to_wstring(index + 1) + L". " + track.title;
    DrawTextW(dc,title.c_str(),-1,&titleBounds,DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (track.durationSeconds >= 0) {
        auto durationBounds = bounds; durationBounds.left = titleBounds.right; durationBounds.right -= S(4);
        const auto duration = mdamp::FormatDuration(static_cast<std::uint64_t>(track.durationSeconds));
        DrawTextW(dc,duration.c_str(),-1,&durationBounds,DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }
    if ((item.itemState & ODS_FOCUS) != 0 && GetFocus() == app->list) DrawFocusRect(dc,&bounds);
    SelectObject(dc,old);
}
void PlaylistContext(POINT point) {
    const auto menu = CreatePopupMenu(); AddMenuItem(menu, PlaySelection, L"Play selected\tEnter"); AddMenuItem(menu, Remove, L"Remove selected\tDelete"); AddMenuItem(menu, Clear, L"Clear playlist");
    AddMenuItem(menu, SelectAll, L"Select all\tCtrl+A"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AddMenuItem(menu, SortTitle, L"Sort by title"); AddMenuItem(menu, Reverse, L"Reverse order"); AddMenuItem(menu, Randomize, L"Randomize order"); AddMenuItem(menu, SaveList, L"Save playlist...");
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, app->playlist, nullptr); DestroyMenu(menu); if (command) Dispatch(static_cast<int>(command));
}
void AuxiliaryMenu(HWND window,int x,POINT screen) {
    if (window == app->playlist && x >= 34 && x < 86) { PlaylistContext(screen); return; }
    if (window == app->playlist && x < 34) { Menu(0,window,screen); return; }
    const auto menu = CreatePopupMenu();
    if (window == app->playlist && x < 118) {
        AddMenuItem(menu,SortTitle,L"Sort by title"); AddMenuItem(menu,Reverse,L"Reverse order"); AddMenuItem(menu,Randomize,L"Randomize order");
    } else if (window == app->library && x < 50) {
        AddMenuItem(menu,Folder,L"Scan music folder..."); AddMenuItem(menu,Add,L"Add library to playlist");
    } else if (window == app->library && x < 110) {
        AddMenuItem(menu,Preferences,L"Preferences..."); AddMenuItem(menu,VisualizationWindow,L"Visualization");
    } else AddMenuItem(menu,About,L"About MdAmp...");
    const auto selected = TrackPopupMenu(menu,TPM_RETURNCMD | TPM_RIGHTBUTTON,screen.x,screen.y,0,window,nullptr); DestroyMenu(menu);
    if (selected == Folder && window == app->library) LoadLibraryFolder();
    else if (selected == Add && window == app->library) AddTracks(app->libraryTracks);
    else if (selected) Dispatch(static_cast<int>(selected));
}
LRESULT CALLBACK AuxiliaryProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(window,&point);
        RECT bounds{}; GetClientRect(window,&bounds); const int edge = S(4);
        const bool left = point.x < edge, right = point.x >= bounds.right - edge;
        const bool top = point.y < edge, bottom = point.y >= bounds.bottom - edge;
        if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_NCCALCSIZE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
        const auto palette = mdamp::ThemePalette(app->settings.theme); const auto brush = CreateSolidBrush(palette.background); FillRect(dc, &client, brush); DeleteObject(brush);
        const auto saved = SaveDC(dc); SetMapMode(dc, MM_ANISOTROPIC); SetWindowExtEx(dc, 1, 1, nullptr); SetViewportExtEx(dc, app->settings.scale, app->settings.scale, nullptr);
        RECT logical{0, 0, client.right / app->settings.scale, client.bottom / app->settings.scale};
        if (window == app->playlist) {
            const auto audio = app->audio.Snapshot();
            const auto elapsed = mdamp::FormatDuration(static_cast<std::uint64_t>(std::max(0.0,audio.position)));
            std::uint64_t totalSeconds = 0; bool allKnown = !app->tracks.empty();
            for (const auto& track : app->tracks) {
                if (track.durationSeconds < 0) allKnown = false;
                else totalSeconds += static_cast<std::uint64_t>(track.durationSeconds);
            }
            const auto total = allKnown ? mdamp::FormatDuration(totalSeconds) : L"--:--";
            mdamp::PaintPlaylistChrome(dc,logical.right,app->playlistShade ? 164 : logical.bottom,app->settings.theme,elapsed + L"/" + total);
        } else mdamp::PaintFrame(dc, logical, window == app->library ? L"MdAmp Media Library" : L"MdAmp Visualization", app->settings.theme);
        if (window == app->library) {
            const auto font = CreateFontW(-9,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
            const auto previous = SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,palette.text);
            TextOutW(dc,8,28,L"File",4);
            if (window == app->playlist) { TextOutW(dc,54,28,L"Playlist",8); TextOutW(dc,119,28,L"Sort",4); TextOutW(dc,170,28,L"Help",4); }
            else { TextOutW(dc,54,28,L"View",4); TextOutW(dc,115,28,L"Help",4); }
            SelectObject(dc,previous); DeleteObject(font);
        }
        RestoreDC(dc, saved);
        if (window == app->visualization) {
            const auto spectrum = app->audio.Snapshot().spectrum; const auto bars = CreateSolidBrush(palette.accent);
            for (std::size_t i = 0; i < spectrum.size(); ++i) {
                const int left = static_cast<int>(i) * (client.right - 24) / 32 + 12, right = static_cast<int>(i + 1) * (client.right - 24) / 32 + 10;
                const float amplitude = std::clamp(spectrum[i], 0.0f, 1.0f);
                RECT bar{left, client.bottom - 14 - static_cast<int>(amplitude * std::max(1L, client.bottom - S(36))), right, client.bottom - 14}; FillRect(dc, &bar, bars);
            }
            DeleteObject(bars);
        }
        EndPaint(window, &paint); return 0;
    }
    case WM_SIZE:
        if (window == app->playlist && wParam != SIZE_MINIMIZED) {
            RECT client{}; GetClientRect(window,&client);
            app->settings.playlistWidth = std::clamp(static_cast<int>(client.right) / app->settings.scale,354,2048);
            if (!app->playlistShade) app->settings.playlistHeight = std::clamp(static_cast<int>(client.bottom) / app->settings.scale,164,2048);
        }
        LayoutAuxiliary(window);
        if (window == app->playlist) PositionWindows();
        return 0;
    case WM_MOVE: if (!app->moving) { if (window == app->playlist) { app->attachedPlaylist = false; app->settings.playlistDocked = false; } if (window == app->library) app->attachedLibrary = false; } return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {S(354), S(window == app->playlist && app->playlistShade ? 17 : 164)};
        if (window == app->playlist && app->playlistShade) limits->ptMaxTrackSize.y = S(17);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (window == app->playlist) {
            RECT client{}; GetClientRect(window,&client);
            POINT point{GET_X_LPARAM(lParam) / app->settings.scale,GET_Y_LPARAM(lParam) / app->settings.scale};
            const int width = client.right / app->settings.scale, height = client.bottom / app->settings.scale;
            const auto layout = mdamp::ComputePlaylistLayout(width,height);
            if (PtInRect(&layout.close,point)) { SendMessageW(window,WM_CLOSE,0,0); return 0; }
            if (PtInRect(&layout.shade,point)) {
                app->playlistShade = !app->playlistShade;
                ShowWindow(app->query,app->playlistShade ? SW_HIDE : SW_SHOWNA); ShowWindow(app->list,app->playlistShade ? SW_HIDE : SW_SHOWNA);
                const auto style = GetWindowLongPtrW(window,GWL_STYLE);
                SetWindowLongPtrW(window,GWL_STYLE,app->playlistShade ? style & ~static_cast<LONG_PTR>(WS_THICKFRAME) : style | WS_THICKFRAME);
                app->moving = true;
                SetWindowPos(window,nullptr,0,0,client.right,S(app->playlistShade ? 17 : app->settings.playlistHeight),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                app->moving = false;
                PositionWindows(); return 0;
            }
            if (!app->playlistShade) {
                const auto action = mdamp::PlaylistFooterHit(point.x,point.y,width,height);
                if (action != mdamp::PlaylistFooterAction::None) {
                    POINT screen{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ClientToScreen(window,&screen);
                    PlaylistFooterMenu(action,screen); return 0;
                }
                if (point.y >= 17 && point.y < 33) {
                    POINT screen{GET_X_LPARAM(lParam),S(33)}; ClientToScreen(window,&screen);
                    AuxiliaryMenu(window,point.x,screen); return 0;
                }
                if (point.x >= width - 31 && point.y >= 39 && point.y < 66) { SetFocus(app->query); return 0; }
            }
            if (point.y < 17) SendMessageW(window,WM_NCLBUTTONDOWN,HTCAPTION,0);
            return 0;
        }
        if (GET_Y_LPARAM(lParam) >= S(17) && GET_Y_LPARAM(lParam) < S(33) && window != app->visualization) {
            POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ClientToScreen(window,&point); AuxiliaryMenu(window,GET_X_LPARAM(lParam) / app->settings.scale,point); return 0;
        }
        if (GET_Y_LPARAM(lParam) < S(17)) { RECT client{}; GetClientRect(window, &client); if (GET_X_LPARAM(lParam) > client.right - S(22)) SendMessageW(window, WM_CLOSE, 0, 0); else SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0); } return 0;
    case WM_DRAWITEM:
        if (wParam == TrackList) { DrawPlaylistRow(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)); return TRUE; } break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == app->query) {
            const auto dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc,mdamp::PlaylistText(app->settings.theme)); SetBkColor(dc,mdamp::PlaylistBackground(app->settings.theme));
            return reinterpret_cast<LRESULT>(app->playlistBrush);
        } break;
    case WM_COMMAND:
        if (LOWORD(wParam) == Query && HIWORD(wParam) == EN_CHANGE) RefreshPlaylist();
        else if (window == app->library && LOWORD(wParam) == Folder) LoadLibraryFolder();
        else if (window == app->library && LOWORD(wParam) == Add) AddTracks(app->libraryTracks);
        else Dispatch(LOWORD(wParam)); return 0;
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification->code == NM_DBLCLK) {
            if (notification->hwndFrom == app->list) { const auto selected = SelectedTracks(); if (!selected.empty()) PlayAt(selected.front()); }
            else if (notification->hwndFrom == app->libraryList) { const int row = ListView_GetNextItem(app->libraryList, -1, LVNI_SELECTED);
                if (row >= 0 && static_cast<std::size_t>(row) < app->libraryTracks.size() && AddTracks({app->libraryTracks[static_cast<std::size_t>(row)]})) PlayAt(static_cast<int>(app->tracks.size()) - 1); }
        } else if (notification->code == LVN_KEYDOWN && notification->hwndFrom == app->list) {
            const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
            if (key->wVKey == VK_DELETE) Dispatch(Remove);
            else if (key->wVKey == VK_RETURN) { const auto selected = SelectedTracks(); if (!selected.empty()) PlayAt(selected.front()); }
        }
        return 0;
    }
    case WM_CONTEXTMENU: if (window == app->playlist) { POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; if (point.x == -1) GetCursorPos(&point); PlaylistContext(point); } return 0;
    case WM_CLOSE: ShowWindow(window, SW_HIDE); if (window == app->playlist) { app->settings.playlistVisible = false; PositionWindows(); } if (window == app->library) app->settings.libraryVisible = false; return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND PrefControl(const wchar_t* type, const wchar_t* label, int id, int x, int y, int width, int height, DWORD style = 0) {
    const auto control = Control(app->prefs, type, label, id, x, y, width, height, style); app->prefControls.push_back(control); return control;
}
void PrefLabel(const wchar_t* label, int y, int height = 30) { PrefControl(L"STATIC", label, -1, 215, y, 510, height, SS_LEFT); }
void PrefSlider(int id, int y, int minimum, int maximum, int value) {
    const auto control = PrefControl(TRACKBAR_CLASSW, L"", id, 215, y, 480, 35, TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP);
    SendMessageW(control, TBM_SETRANGEMIN, FALSE, minimum); SendMessageW(control, TBM_SETRANGEMAX, FALSE, maximum); SendMessageW(control, TBM_SETPOS, TRUE, value);
}
void RefreshPreferences() {
    if (!app->prefs) return;
    for (const auto window : app->prefControls) DestroyWindow(window);
    app->prefControls.clear(); PrefLabel(L"MdAmp " MDAMP_VERSION_W L" — Preferences", 14);
    switch (app->prefPage) {
    case 0:
        PrefLabel(L"General preferences", 54);
        Check(PrefControl(L"BUTTON", L"Always on top", PrefTop, 215, 92, 460, 26, BS_AUTOCHECKBOX | WS_TABSTOP), app->settings.alwaysOnTop);
        Check(PrefControl(L"BUTTON", L"Show equalizer / options drawer", PrefDrawer, 215, 126, 460, 26, BS_AUTOCHECKBOX | WS_TABSTOP), app->settings.drawer);
        PrefLabel(L"Settings and the session playlist are saved automatically in your\nLocalAppData\\MdAmp folder. Original Winamp settings are not read.", 182, 70);
        PrefLabel(L"Drag files onto the main player. Double-click its title to shade.\nPlaylist and library windows can be dragged independently.", 270, 60); break;
    case 1:
        PrefLabel(L"Playback: volume (0–100%)", 54); PrefSlider(PrefVolume, 84, 0, 100, static_cast<int>(app->settings.volume * 100));
        PrefLabel(L"Balance: left / center / right", 132); PrefSlider(PrefPan, 162, -100, 100, static_cast<int>(app->settings.pan * 100));
        Check(PrefControl(L"BUTTON", L"Shuffle playback", PrefShuffle, 215, 218, 460, 26, BS_AUTOCHECKBOX | WS_TABSTOP), app->settings.shuffle);
        PrefLabel(L"Repeat mode", 258);
        { const auto combo = PrefControl(L"COMBOBOX", L"", PrefRepeat, 215, 290, 300, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
          for (const auto text : {L"Off", L"Playlist", L"Track"}) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
          SendMessageW(combo, CB_SETCURSEL, app->settings.repeat == -1 ? 2 : app->settings.repeat == 1 ? 1 : 0, 0); } break;
    case 2:
        PrefLabel(L"Modern-style appearance: original vector artwork", 54);
        { const auto combo = PrefControl(L"COMBOBOX", L"", PrefTheme, 215, 90, 410, 200, CBS_DROPDOWNLIST | WS_TABSTOP);
          for (const auto& theme : mdamp::ThemeNames()) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(theme.c_str())); SendMessageW(combo, CB_SETCURSEL, app->settings.theme, 0); }
        PrefLabel(L"Player scale", 145);
        { const auto combo = PrefControl(L"COMBOBOX", L"", PrefScale, 215, 178, 200, 100, CBS_DROPDOWNLIST | WS_TABSTOP);
          SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"100%")); SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"200%")); SendMessageW(combo, CB_SETCURSEL, app->settings.scale - 1, 0); }
        PrefLabel(L"Layouts follow the 354-wide Modern player, with a 116-unit drawer.\nThese are new themes, not Winamp skin/MAKI packages.", 248, 70); break;
    case 3:
        PrefLabel(L"Playlist Editor", 54);
        PrefLabel(L"UTF-8 M3U / M3U8 import and export.\nSearch filters titles and paths without altering the list.\nDouble-click / Enter: play. Delete: remove from playlist only.\nRight-click: sort, reverse, randomize, select, clear.\nCtrl+S: save; Ctrl+Shift+O: load.", 96, 160);
        PrefLabel(L"No command here deletes audio files from disk.\nLibrary scanning is limited to WAV / MP3 / FLAC and 10,000 entries.", 280, 70); break;
    case 4:
        Check(PrefControl(L"BUTTON", L"Enable 10-band equalizer", PrefEq, 215, 54, 460, 26, BS_AUTOCHECKBOX | WS_TABSTOP), app->settings.eqEnabled);
        PrefLabel(L"Preamp (-12 to +12 dB)", 91); PrefSlider(PrefPreamp, 118, -120, 120, static_cast<int>(app->settings.preamp * 10));
        PrefLabel(L"Bands: 60 / 170 / 310 / 600 / 1k / 3k / 6k / 12k / 14k / 16k Hz", 170);
        for (int i = 0; i < 10; ++i) {
            const auto control = PrefControl(TRACKBAR_CLASSW, L"", PrefBand0 + i, 218 + i * 48, 205, 40, 165, TBS_VERT | TBS_BOTH | WS_TABSTOP);
            SendMessageW(control, TBM_SETRANGEMIN, FALSE, -120); SendMessageW(control, TBM_SETRANGEMAX, FALSE, 120); SendMessageW(control, TBM_SETPOS, TRUE, static_cast<int>(-app->settings.eq[static_cast<std::size_t>(i)] * 10));
        }
        PrefLabel(L"Changes apply immediately. Presets are in the main EQ drawer.", 385); break;
    case 5:
        PrefLabel(L"Plug-ins: DSP / Effect", 54); PrefLabel(app->plugin.api ? L"Loaded: MdAmp Stereo Enhancer (MdAmp DSP ABI v1)" : L"MdAmpStereo.dll is missing or incompatible.", 92);
        PrefLabel(L"Stereo width: off → enhanced (preserves mono center)", 134); PrefSlider(PrefStereo, 165, 0, 100, static_cast<int>(app->stereoWidth * 100));
        EnableWindow(GetDlgItem(app->prefs,PrefStereo),app->plugin.api != nullptr);
        PrefLabel(L"The DLL is loaded only from this program's plugins folder.\nIt is our own module, not a Winamp DSP DLL.\nLegacy Winamp plug-ins, AVS, MilkDrop and MAKI skins\nare not binary compatible.", 240, 130); break;
    case 6:
        PrefLabel(L"Plug-ins: Input / Output / Visualization", 54);
        PrefLabel(L"Input: PCM WAV, MP3, FLAC (miniaudio 0.11.23).\nOutput: Windows WASAPI, stereo float, 48 kHz.\nDSP: 10-band biquad EQ + preamp + balance.\nVisualization: 32-band live spectrum window.\n\nNo CD ripping, video, online services, device sync, encoders,\nDirectShow, legacy module formats or Winamp DLL loader yet.", 98, 210);
        PrefControl(L"BUTTON", L"Toggle visualization", VisualizationWindow, 215, 340, 250, 30, WS_TABSTOP); break;
    case 7:
        PrefLabel(L"Keyboard shortcuts", 54);
        PrefLabel(L"Z / X / C / V / B: previous / play / pause / stop / next\nSpace: play/pause; Left/Right: seek 5 seconds\nUp/Down: volume; S: shuffle; R: repeat\nL / Ctrl+O: open files; Ctrl+L: append files; J: search playlist\nCtrl+P: preferences; Alt+E: playlist; Alt+G: equalizer\nAlt+L: library; Ctrl+T: on top; Ctrl+W: windowshade\nCtrl+S: save playlist; Ctrl+Shift+O: load playlist\nCtrl+Shift+K: visualization; Alt+3: file info\nPlaylist: Enter play, Delete remove, Ctrl+A select all\nAlt+F/P/O/I/H: File / Play / Options / View / Help menu", 98, 300); break;
    }
    InvalidateRect(app->prefs, nullptr, TRUE);
}
void ApplyPreference(HWND control) {
    if (!control) return; const int id = GetDlgCtrlID(control);
    if (id == PrefTop) app->settings.alwaysOnTop = Checked(control);
    else if (id == PrefDrawer) app->settings.drawer = Checked(control);
    else if (id == PrefShuffle) app->settings.shuffle = Checked(control);
    else if (id == PrefEq) app->settings.eqEnabled = Checked(control);
    else if (id == PrefTheme) app->settings.theme = static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
    else if (id == PrefScale) Dispatch(SendMessageW(control, CB_GETCURSEL, 0, 0) == 0 ? ScaleOne : ScaleTwo);
    else if (id == PrefRepeat) { const auto selection = SendMessageW(control, CB_GETCURSEL, 0, 0); app->settings.repeat = selection == 2 ? -1 : selection == 1 ? 1 : 0; }
    else if (id == PrefVolume) app->settings.volume = static_cast<float>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 100;
    else if (id == PrefPan) app->settings.pan = static_cast<float>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 100;
    else if (id == PrefPreamp) app->settings.preamp = static_cast<float>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 10;
    else if (id == PrefStereo) app->stereoWidth = static_cast<float>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 100;
    else if (id >= PrefBand0 && id < PrefBand0 + 10) app->settings.eq[static_cast<std::size_t>(id - PrefBand0)] = -static_cast<float>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 10;
    ApplyAudioSettings(); ThemeChanged(); ResizePlayer();
}
LRESULT CALLBACK PreferencesProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        app->prefs = window; app->prefTree = Control(window, WC_TREEVIEWW, L"", Tree, 12, 12, 185, 425, TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE);
        const std::array<const wchar_t*, 8> pages{{L"General Preferences",L"Playback",L"Skins / Modern",L"Playlist",L"Equalizer",L"Plug-ins / DSP",L"Input / Output / Vis",L"Keyboard Shortcuts"}};
        for (std::size_t i = 0; i < pages.size(); ++i) { TVINSERTSTRUCTW item{}; item.hParent = TVI_ROOT; item.hInsertAfter = TVI_LAST; item.item.mask = TVIF_TEXT | TVIF_PARAM; item.item.pszText = const_cast<wchar_t*>(pages[i]); item.item.lParam = static_cast<LPARAM>(i); const auto inserted = TreeView_InsertItem(app->prefTree, &item); if (i == 0) TreeView_SelectItem(app->prefTree, inserted); }
        Control(window, L"BUTTON", L"Apply", PrefApply, 520, 444, 95, 28, WS_TABSTOP); Control(window, L"BUTTON", L"Close", PrefClose, 630, 444, 95, 28, WS_TABSTOP | BS_DEFPUSHBUTTON); RefreshPreferences(); return 0;
    }
    case WM_NOTIFY: if (reinterpret_cast<const NMHDR*>(lParam)->code == TVN_SELCHANGEDW) { const auto* selection = reinterpret_cast<const NMTREEVIEWW*>(lParam); app->prefPage = static_cast<int>(selection->itemNew.lParam); RefreshPreferences(); } return 0;
    case WM_HSCROLL: case WM_VSCROLL: ApplyPreference(reinterpret_cast<HWND>(lParam)); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == PrefClose) SendMessageW(window, WM_CLOSE, 0, 0);
        else if (LOWORD(wParam) == PrefApply) SaveState(); else if (LOWORD(wParam) == VisualizationWindow) Dispatch(VisualizationWindow);
        else if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE) ApplyPreference(reinterpret_cast<HWND>(lParam)); return 0;
    case WM_CLOSE: SaveState(); DestroyWindow(window); return 0;
    case WM_DESTROY: app->prefControls.clear(); app->prefTree = nullptr; app->prefs = nullptr; return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
bool HandleKey(const MSG& message) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    wchar_t focusedClass[64]{}; GetClassNameW(GetFocus(), focusedClass, 64);
    const bool editing = _wcsicmp(focusedClass, L"EDIT") == 0 || (app->prefs && IsChild(app->prefs, GetFocus()));
    const auto key = static_cast<int>(message.wParam); int command = 0;
    if (key == VK_ESCAPE && app->prefs && (message.hwnd == app->prefs || IsChild(app->prefs,message.hwnd))) { SendMessageW(app->prefs,WM_CLOSE,0,0); return true; }
    if (control) {
        if (key == 'P') command = Preferences; else if (key == 'O') command = shift ? LoadList : Open; else if (key == 'L') command = Add;
        else if (key == 'S' && !editing) command = SaveList; else if (key == 'T' && !editing) command = AlwaysOnTop; else if (key == 'W' && !editing) command = Shade;
        else if (key == 'K' && shift) command = VisualizationWindow; else if (key == 'A' && GetFocus() == app->list) command = SelectAll;
    } else if (alt) {
        if (key == VK_F4) { if (app->prefs && (message.hwnd == app->prefs || IsChild(app->prefs,message.hwnd))) SendMessageW(app->prefs,WM_CLOSE,0,0); else Dispatch(Exit); return true; }
        const auto root = GetAncestor(message.hwnd,GA_ROOT);
        if ((root == app->playlist && (key == 'F' || key == 'P' || key == 'S' || key == 'H')) ||
            (root == app->library && (key == 'F' || key == 'I' || key == 'H'))) {
            RECT rect{}; GetWindowRect(root,&rect);
            AuxiliaryMenu(root,key == 'F' ? 8 : key == 'P' || key == 'I' ? 54 : key == 'S' ? 92 : 125,{rect.left + S(8),rect.top + S(33)}); return true;
        }
        if (key == 'E') command = PlaylistWindow; else if (key == 'G') command = EqualizerWindow; else if (key == 'L') command = LibraryWindow; else if (key == '3') command = FileInfo;
        else if (key == 'F' || key == 'P' || key == 'O' || key == 'I' || key == 'H') { RECT rect{}; GetWindowRect(app->player, &rect); Menu(key == 'F' ? 0 : key == 'P' ? 1 : key == 'O' ? 2 : key == 'I' ? 3 : 4, app->player, {rect.left + S(8), rect.top + S(33)}); return true; }
    } else if (!editing) {
        switch (key) {
        case 'Z': command = Prev; break; case 'X': command = Play; break; case 'C': command = Pause; break; case 'V': command = Stop; break; case 'B': command = Next; break; case VK_SPACE: command = Pause; break;
        case 'S': command = Shuffle; break; case 'R': command = Repeat; break; case 'L': command = Open; break; case 'J': command = Jump; break;
        case VK_LEFT: if (GetFocus() == app->list || GetFocus() == app->libraryList) return false; app->audio.Seek(app->audio.Snapshot().position - 5); return true;
        case VK_RIGHT: if (GetFocus() == app->list || GetFocus() == app->libraryList) return false; app->audio.Seek(app->audio.Snapshot().position + 5); return true;
        case VK_UP: case VK_DOWN: if (GetFocus() == app->list || GetFocus() == app->libraryList) return false; app->settings.volume = std::clamp(app->settings.volume + (key == VK_UP ? .02f : -.02f), 0.0f, 1.0f); ApplyAudioSettings(); return true;
        }
    } else if (GetFocus() == app->query && key == VK_RETURN) { if (!app->visibleTracks.empty()) PlayAt(app->visibleTracks[0]); return true; }
    if (command) { Dispatch(command); return true; } return false;
}
bool RegisterClasses() {
    const std::array<const wchar_t*, 5> names{{kPlayerClass,kPlaylistClass,kLibraryClass,kVisClass,kPrefsClass}};
    for (const auto name : names) {
        WNDCLASSEXW definition{}; definition.cbSize = sizeof(definition); definition.hInstance = app->instance; definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        definition.hIcon = LoadIconW(nullptr, IDI_APPLICATION); definition.lpszClassName = name; definition.style = CS_DBLCLKS;
        definition.lpfnWndProc = name == kPlayerClass ? PlayerProcedure : name == kPrefsClass ? PreferencesProcedure : AuxiliaryProcedure;
        if (name == kPrefsClass) definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassExW(&definition)) return false;
    }
    return true;
}
bool CreateWindows() {
    app->font = CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Tahoma");
    app->player = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, kPlayerClass, L"MdAmp " MDAMP_VERSION_W, WS_POPUP | WS_MINIMIZEBOX | WS_THICKFRAME,
        app->settings.x, app->settings.y, S(PlayerWidth()), S(PlayerHeight()), nullptr, nullptr, app->instance, nullptr);
    if (!app->player) return false;
    app->moving = true;
    app->playlist = CreateWindowExW(WS_EX_TOOLWINDOW,kPlaylistClass,L"MdAmp Playlist Editor",WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,app->settings.x,app->settings.y + S(PlayerHeight() + 1),S(app->attachedPlaylist ? PlayerWidth() : app->settings.playlistWidth),S(app->settings.playlistHeight),app->player,nullptr,app->instance,nullptr);
    app->library = CreateWindowExW(WS_EX_TOOLWINDOW,kLibraryClass,L"MdAmp Media Library",WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,app->settings.x,app->settings.y + S(PlayerHeight() + app->settings.playlistHeight + 1),S(790),S(260),app->player,nullptr,app->instance,nullptr);
    app->visualization = CreateWindowExW(WS_EX_TOOLWINDOW,kVisClass,L"MdAmp Visualization",WS_POPUP | WS_THICKFRAME,app->settings.x + 80,app->settings.y + 80,S(354),S(200),app->player,nullptr,app->instance,nullptr);
    app->moving = false; if (!app->playlist || !app->library || !app->visualization) return false;
    app->query = Control(app->playlist,L"EDIT",L"",Query,S(10),S(43),S(400),S(20),ES_AUTOHSCROLL | WS_TABSTOP);
    SetWindowSubclass(app->query,QueryProcedure,1,0);
    app->playlistBrush = CreateSolidBrush(mdamp::PlaylistBackground());
    app->list = Control(app->playlist,WC_LISTVIEWW,L"Playlist",TrackList,S(7),S(66),S(420),S(198),LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_OWNERDRAWFIXED | WS_TABSTOP);
    SetWindowTheme(app->list,L"",L"");
    AddColumn(app->list,0,L"Title",S(360)); AddColumn(app->list,1,L"Time",S(64));
    UpdatePlaylistFonts();
    Control(app->library,L"BUTTON",L"Open folder",Folder,S(8),S(43),S(110),S(22),WS_TABSTOP);
    Control(app->library,L"BUTTON",L"Add all to playlist",Add,S(126),S(43),S(145),S(22),WS_TABSTOP);
    app->libraryList = Control(app->library,WC_LISTVIEWW,L"Media Library",LibraryList,S(8),S(54),S(770),S(195),LVS_REPORT | WS_TABSTOP);
    AddColumn(app->libraryList,0,L"Title",S(280)); AddColumn(app->libraryList,1,L"File",S(440));
    LayoutAuxiliary(app->playlist); LayoutAuxiliary(app->library); ThemeChanged(); RefreshPlaylist(); DragAcceptFiles(app->player, TRUE);
    return app->query && app->list && app->libraryList;
}
int RunSmoke() {
    if (!app->player || !app->list || !app->query || !app->libraryList) return 10;
    bool play = false, seek = false, volume = false, eq = false;
    for (const auto& region : mdamp::PlayerHits(false,true,0)) {
        play |= region.action == mdamp::Action::Play; seek |= region.action == mdamp::Action::Seek; volume |= region.action == mdamp::Action::Volume; eq |= region.action == mdamp::Action::Band9;
        if (region.rect.right <= region.rect.left || region.rect.bottom <= region.rect.top) return 11;
    }
    if (!play || !seek || !volume || !eq) return 12;
    // Exercise live native child geometry and controller search without touching
    // application data or opening an audio device. These are not pixel goldens.
    for (const int scale : {1,2}) {
        app->settings.scale = scale; app->settings.playerWidth = 487;
        UpdatePlaylistFonts(); ResizePlayer();
        RECT player{}, playlist{}, client{};
        GetWindowRect(app->player,&player); GetWindowRect(app->playlist,&playlist); GetClientRect(app->player,&client);
        if (client.right != S(487) || client.bottom != S(280) || playlist.left != player.left ||
            playlist.top != player.bottom + S(1) || playlist.right - playlist.left != S(487)) return 50;
        auto first = mdamp::MakeTrack(L"synthetic-one.wav"), second = mdamp::MakeTrack(L"synthetic-two.wav");
        first.title = L"needle"; first.durationSeconds = 125; second.title = L"other";
        app->tracks = {first,second}; RefreshPlaylist();
        wchar_t duration[32]{}; ListView_GetItemText(app->list,0,1,duration,32);
        if (std::wstring(duration) != L"2:05" || ListView_GetItemCount(app->list) != 2) return 51;
        SetWindowTextW(app->query,L"needle");
        if (ListView_GetItemCount(app->list) != 1 || app->visibleTracks[0] != 0) return 52;
        SetWindowTextW(app->query,L"");
        if (ListView_GetItemCount(app->list) != 2) return 53;
        app->tracks.assign(200,first); RefreshPlaylist();
        GetClientRect(app->list,&client);
        if (ListView_GetColumnWidth(app->list,0) + ListView_GetColumnWidth(app->list,1) > client.right) return 54;
        if ((GetWindowLongPtrW(app->list,GWL_STYLE) & WS_HSCROLL) != 0) return 541;
        const auto expected = mdamp::ComputePlaylistLayout(487,app->settings.playlistHeight);
        GetWindowRect(app->query,&client); MapWindowPoints(nullptr,app->playlist,reinterpret_cast<POINT*>(&client),2);
        if (client.left != S(expected.query.left) || client.top != S(expected.query.top) || client.right != S(expected.query.right)) return 55;
        const int savedHeight = app->settings.playlistHeight;
        SendMessageW(app->playlist,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(S(expected.shade.left + 3),S(expected.shade.top + 3)));
        GetClientRect(app->playlist,&client);
        if (!app->playlistShade) return 561;
        if (client.bottom != S(17)) return 562;
        if ((GetWindowLongPtrW(app->query,GWL_STYLE) & WS_VISIBLE) != 0) return 563;
        if (app->settings.playlistHeight != savedHeight) return 564;
        SendMessageW(app->playlist,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(S(expected.shade.left + 3),S(expected.shade.top + 3)));
        GetClientRect(app->playlist,&client);
        if (app->playlistShade || client.bottom != S(savedHeight) || (GetWindowLongPtrW(app->query,GWL_STYLE) & WS_VISIBLE) == 0) return 57;
        app->tracks.clear(); RefreshPlaylist();
    }
    app->settings.scale = 1; ResizePlayer(); UpdatePlaylistFonts();
    Dispatch(Shade); RECT shaded{}; GetClientRect(app->player,&shaded);
    if (shaded.bottom != 17) return 58;
    Dispatch(Shade);
    const auto shade = mdamp::ComputePlaylistLayout(487,app->settings.playlistHeight).shade;
    SendMessageW(app->playlist,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(shade.left + 3,shade.top + 3));
    app->attachedPlaylist = false; app->settings.playlistDocked = false;
    const auto savedHeight = app->settings.playlistHeight;
    Dispatch(ScaleTwo); GetClientRect(app->playlist,&shaded);
    if (!app->playlistShade || shaded.bottom != S(17) || app->settings.playlistHeight != savedHeight || app->attachedPlaylist) return 59;
    Dispatch(ScaleOne);
    SendMessageW(app->playlist,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(shade.left + 3,shade.top + 3));
    app->attachedPlaylist = true; app->settings.playlistDocked = true;
    SetWindowPos(app->playlist,nullptr,0,0,487,500,SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT playlist{}, library{}; GetWindowRect(app->playlist,&playlist); GetWindowRect(app->library,&library);
    if (library.top != playlist.bottom || app->settings.playlistHeight != 500) return 60;
    app->settings.playlistHeight = savedHeight; ResizePlayer();
    ShowPreferences(); if (!app->prefs || !app->prefTree) return 13;
    for (int page = 0; page < 8; ++page) { app->prefPage = page; RefreshPreferences(); if (app->prefControls.empty()) return 14; }
    DestroyWindow(app->prefs);
    const auto dc = GetDC(app->player); const auto memory = CreateCompatibleDC(dc); const auto bitmap = CreateCompatibleBitmap(dc,708,560); const auto old = SelectObject(memory,bitmap);
    for (int theme = 0; theme < static_cast<int>(mdamp::ThemeNames().size()); ++theme) for (int tab = 0; tab < 3; ++tab) { app->view.theme = theme; app->view.tab = tab; mdamp::PaintPlayer(memory,app->view); }
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(app->player,dc); return 0;
}
int AudioSmoke(const std::wstring& file) {
    mdamp::AudioEngine audio; std::wstring error; if (!audio.Open(file,error)) return 20;
    audio.SetVolume(0); if (!audio.Play()) return 21;
    std::this_thread::sleep_for(std::chrono::milliseconds(350)); const auto a = audio.Snapshot(); if (a.position < .1 || !a.playing) return 22;
    audio.Pause(); const auto paused = audio.Snapshot().position; std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (std::abs(audio.Snapshot().position - paused) > .05) return 23;
    audio.Seek(.1); if (std::abs(audio.Snapshot().position - .1) > .05) return 24; if (!audio.Play()) return 25;
    audio.SetEqualizer(true,{{3,2,1,0,0,0,0,1,2,3}},-4); std::this_thread::sleep_for(std::chrono::milliseconds(150)); audio.Stop();
    return audio.Snapshot().playing || audio.Snapshot().position > .05 ? 26 : 0;
}
int RunIntegration(const std::filesystem::path& file) {
    app->settings.volume = 0;
    if (!app->plugin.Load()) return 30;
    app->audio.SetDspCallback(PluginHost::Process,&app->plugin); app->stereoWidth = .5f;
    auto first = mdamp::MakeTrack(file), second = first; first.title = L"A duplicate"; second.title = L"B duplicate";
    if (!AddTracks({first,second}) || !PlayAt(1)) return 31;
    Dispatch(Reverse);
    if (app->current != 0 || app->tracks[0].title != L"B duplicate") return 32;
    Dispatch(Pause); if (!app->paused) return 33;
    Dispatch(Clear); Dispatch(Pause);
    if (!app->tracks.empty() || app->current != -1 || app->paused || app->audio.Snapshot().duration != 0 || app->audio.Snapshot().playing) return 34;
    AddTracks({first});
    std::vector<std::filesystem::path> excessive(10001,file);
    AppendFiles(excessive,true,false);
    if (app->tracks.size() != 1 || app->tracks[0].title != first.title) return 35;
    if (!PlayAt(0)) return 36;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (app->audio.Snapshot().position < .05) return 37;
    ListView_SetItemState(app->list,0,LVIS_SELECTED,LVIS_SELECTED);
    Dispatch(Remove); Dispatch(Pause);
    if (!app->tracks.empty() || app->audio.Snapshot().duration != 0) return 38;
    Dispatch(EqRock); if (!app->settings.eqEnabled || app->settings.eq[0] != 4) return 39;
    Dispatch(EqFlat); if (app->settings.eqEnabled || app->settings.preamp != 0) return 40;
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int count = 0; auto* arguments = CommandLineToArgvW(GetCommandLineW(),&count);
    bool smoke = false; std::wstring audioSmoke, integrationFile; std::vector<std::filesystem::path> initialFiles;
    if (arguments) { for (int i = 1; i < count; ++i) {
        if (std::wstring(arguments[i]) == L"--smoke-test") smoke = true;
        else if (std::wstring(arguments[i]) == L"--audio-smoke" && i + 1 < count) audioSmoke = arguments[++i];
        else if (std::wstring(arguments[i]) == L"--integration-test" && i + 1 < count) { integrationFile = arguments[++i]; smoke = true; }
        else initialFiles.emplace_back(arguments[i]); } LocalFree(arguments); }
    const auto com = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (!audioSmoke.empty()) { const auto result = AudioSmoke(audioSmoke); if (SUCCEEDED(com)) CoUninitialize(); return result; }
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES}; if (!InitCommonControlsEx(&common)) return 1;
    Application application; app = &application; app->instance = instance; app->smoke = smoke;
    if (!smoke) {
        app->dataDirectory = mdamp::DefaultDataDirectory();
        if (app->dataDirectory.empty()) { Error(L"Cannot resolve LocalAppData. No settings or playlists were changed."); if (SUCCEEDED(com)) CoUninitialize(); return 2; }
        app->settings = mdamp::LoadSettings(app->dataDirectory / L"settings.ini");
        app->sessionFile = app->dataDirectory / L"session.m3u8";
        std::wstring error;
        if (std::filesystem::exists(app->sessionFile)) app->tracks = mdamp::LoadPlaylist(app->sessionFile,error);
        if (!error.empty()) {
            app->sessionFile = app->dataDirectory / (L"session-recovered-" + std::to_wstring(GetTickCount64()) + L".m3u8");
            Error(error + L"\nOriginal session preserved. New session will be saved separately.");
        }
    }
    app->stereoWidth = app->settings.stereoWidth;
    app->attachedPlaylist = app->settings.playlistDocked;
    app->settings.scale = std::clamp(app->settings.scale,1,2);
    if (!RegisterClasses() || !CreateWindows()) { if (SUCCEEDED(com)) CoUninitialize(); return 2; }
    int result = 0;
    if (smoke) { result = RunSmoke(); if (result == 0 && !integrationFile.empty()) result = RunIntegration(integrationFile); }
    else {
        if (app->plugin.Load()) app->audio.SetDspCallback(PluginHost::Process,&app->plugin);
        ApplyAudioSettings(); UpdateView(); ShowWindow(app->player,show);
        ShowWindow(app->playlist,app->settings.playlistVisible ? SW_SHOWNOACTIVATE : SW_HIDE); ShowWindow(app->library,app->settings.libraryVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
        ResizePlayer(); SetTimer(app->player,1,50,nullptr); if (!initialFiles.empty()) AppendFiles(initialFiles,true,true);
        MSG message{}; while (true) { const int received = GetMessageW(&message,nullptr,0,0); if (received <= 0) { result = received == 0 ? static_cast<int>(message.wParam) : 3; break; }
            if (HandleKey(message)) continue; if (app->prefs && IsDialogMessageW(app->prefs,&message)) continue; TranslateMessage(&message); DispatchMessageW(&message); }
    }
    app->shuttingDown = true;
    for (const auto window : {app->prefs,app->visualization,app->library,app->playlist,app->player}) if (IsWindow(window)) DestroyWindow(window);
    app->audio.Close();
    if (SUCCEEDED(com)) CoUninitialize(); return result;
}
