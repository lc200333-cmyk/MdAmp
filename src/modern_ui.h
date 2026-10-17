#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string>
#include <vector>

namespace mdamp {
enum class Action {
    None, Previous, Play, Pause, Stop, Next, Open, TogglePlaylist,
    ToggleLibrary, Shuffle, Repeat, ToggleDrawer, DrawerEq, DrawerOptions,
    DrawerThemes, Preferences, Minimize, Shade, Close, Seek, Volume, Pan,
    Preamp, EqToggle, EqAuto, EqPreset, ThemePrevious, ThemeNext,
    ThemeSelect, Visualize, Band0, Band1, Band2, Band3, Band4, Band5,
    Band6, Band7, Band8, Band9
};
struct HitRegion { RECT rect; Action action; };
std::vector<HitRegion> PlayerHits(bool shade, bool drawer, int tab);
struct PlayerView {
    std::wstring title = L"MdAmp", info = L"Ready", time = L"0:00";
    bool playing = false, paused = false, shuffle = false, eqEnabled = false;
    bool drawer = true, shade = false;
    int repeat = 0, tab = 0, theme = 0;
    float volume = .75f, pan = 0, progress = 0, preamp = 0;
    std::array<float, 10> eq{};
    std::array<float, 32> spectrum{};
};
struct Palette { COLORREF background, panel, text, accent, dim; };
Palette ThemePalette(int theme);
std::vector<std::wstring> ThemeNames();
void PaintPlayer(HDC dc, const PlayerView& view);
void PaintFrame(HDC dc, RECT bounds, const wchar_t* title, int theme);
}
