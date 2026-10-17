#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace mdamp {

// Coordinates are logical pixels. The controller applies the same window scale
// to both the painter and these native child-control / pointer-hit rectangles.
struct PlaylistLayout {
    RECT title{}, menu{}, searchBorder{}, query{}, list{}, footer{};
    RECT add{}, remove{}, select{}, misc{}, time{}, manage{}, shade{}, close{};
};

enum class PlaylistFooterAction { None, Add, Remove, Select, Misc, Manage };

PlaylistLayout ComputePlaylistLayout(int width, int height);
PlaylistFooterAction PlaylistFooterHit(int x, int y, int width, int height);

COLORREF PlaylistBackground(int theme = 0);
COLORREF PlaylistText(int theme = 0);
constexpr int kPlaylistQueryFontHeight = 13;
constexpr int kPlaylistListFontHeight = 20;

// Original GDI chrome, reconstructed from observed geometry. The live editable
// search and list HWNDs overlay query/list; this does not paint fake media rows.
void PaintPlaylistChrome(HDC dc, int width, int height, int theme,
    const std::wstring& timeText);

}
