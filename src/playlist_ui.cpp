#include "playlist_ui.h"
#include "modern_ui.h"
#include <algorithm>

namespace mdamp {
namespace {
COLORREF Mix(COLORREF first, COLORREF second, int weight) {
    const auto channel = [weight](int a, int b) {
        return (a * (255 - weight) + b * weight) / 255;
    };
    return RGB(channel(GetRValue(first), GetRValue(second)),
        channel(GetGValue(first), GetGValue(second)),
        channel(GetBValue(first), GetBValue(second)));
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Line(HDC dc, int left, int top, int right, int bottom, COLORREF color) {
    const HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, left, top, nullptr);
    LineTo(dc, right, bottom);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void Frame(HDC dc, RECT rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Gradient(HDC dc, RECT rect, COLORREF first, COLORREF last) {
    const int height = static_cast<int>(rect.bottom - rect.top);
    for (int y = 0; y < height; ++y) {
        Fill(dc, {rect.left, rect.top + y, rect.right, rect.top + y + 1},
            Mix(first, last, y * 255 / std::max(1, height - 1)));
    }
}

void Text(HDC dc, RECT rect, const wchar_t* text, int height,
    COLORREF color, UINT alignment = DT_LEFT, bool bold = false) {
    const HFONT font = CreateFontW(-height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Tahoma");
    const HGDIOBJ oldFont = SelectObject(dc, font);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
        DT_END_ELLIPSIS | alignment);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void OvalButton(HDC dc, RECT rect, const wchar_t* label) {
    const int saved = SaveDC(dc);
    // A path is transformed by the caller's logical-to-device mapping. Raw
    // HRGN coordinates would clip the wrong pixels at a translated/scaled DC.
    BeginPath(dc);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
    EndPath(dc);
    SelectClipPath(dc, RGN_AND);
    Gradient(dc, rect, RGB(255, 255, 255), RGB(131, 137, 148));
    RestoreDC(dc, saved);
    const HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 66, 78));
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    const RECT inner{rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
    Text(dc, inner, label, 10, RGB(13, 18, 29), DT_CENTER);
}

bool Contains(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
}

PlaylistLayout ComputePlaylistLayout(int width, int height) {
    width = std::max(width, 354);
    height = std::max(height, 164);
    PlaylistLayout layout;
    layout.title = {2, 2, width - 2, 17};
    layout.menu = {3, 17, width - 3, 33};
    layout.searchBorder = {7, 39, width - 7, 66};
    layout.query = {10, 43, width - 31, 63};
    layout.list = {7, 66, width - 7, height - 24};
    layout.footer = {3, height - 24, width - 3, height - 3};
    layout.add = {7, height - 21, 46, height - 5};
    layout.remove = {49, height - 21, 88, height - 5};
    layout.select = {91, height - 21, 126, height - 5};
    layout.misc = {129, height - 21, 171, height - 5};
    layout.time = {std::max(179, width - 218), height - 21, width - 116, height - 5};
    layout.manage = {width - 110, height - 21, width - 8, height - 5};
    layout.shade = {width - 29, 4, width - 19, 14};
    layout.close = {width - 17, 4, width - 7, 14};
    return layout;
}

PlaylistFooterAction PlaylistFooterHit(int x, int y, int width, int height) {
    const PlaylistLayout layout = ComputePlaylistLayout(width, height);
    if (Contains(layout.add, x, y)) return PlaylistFooterAction::Add;
    if (Contains(layout.remove, x, y)) return PlaylistFooterAction::Remove;
    if (Contains(layout.select, x, y)) return PlaylistFooterAction::Select;
    if (Contains(layout.misc, x, y)) return PlaylistFooterAction::Misc;
    if (Contains(layout.manage, x, y)) return PlaylistFooterAction::Manage;
    return PlaylistFooterAction::None;
}

COLORREF PlaylistBackground(int theme) {
    return theme == 0 ? RGB(28, 61, 125) : ThemePalette(theme).background;
}

COLORREF PlaylistText(int theme) {
    return theme == 0 ? RGB(187, 210, 255) : ThemePalette(theme).text;
}

void PaintPlaylistChrome(HDC dc, int width, int height, int theme,
    const std::wstring& timeText) {
    const int saved = SaveDC(dc);
    const PlaylistLayout layout = ComputePlaylistLayout(width, height);
    const Palette palette = ThemePalette(theme);
    const COLORREF panel = theme == 0 ? RGB(197, 199, 205) : palette.panel;
    const COLORREF navy = theme == 0 ? RGB(29, 43, 68) : palette.dim;
    Fill(dc, {0, 0, width, height}, RGB(35, 39, 49));
    Gradient(dc, {2, 2, width - 2, height - 2},
        Mix(panel, RGB(255, 255, 255), 80), panel);
    Frame(dc, {1, 1, width - 1, height - 1}, RGB(246, 246, 248));
    Frame(dc, {3, 3, width - 3, height - 3}, RGB(96, 99, 109));
    Gradient(dc, layout.title, RGB(107, 119, 140), navy);
    Line(dc, 4, 16, width - 4, 16, RGB(13, 22, 37));
    const int middle = width / 2;
    Line(dc, 23, 6, middle - 70, 6, RGB(232, 233, 239));
    Line(dc, 23, 8, middle - 70, 8, RGB(103, 119, 145));
    Line(dc, middle + 69, 6, width - 35, 6, RGB(232, 233, 239));
    Line(dc, middle + 69, 8, width - 35, 8, RGB(103, 119, 145));
    Text(dc, {middle - 73, 1, middle + 73, 16}, L"PLAYLIST EDITOR", 11,
        RGB(153, 180, 228), DT_CENTER);
    // Small original geometric window emblem, not a copied bitmap.
    Frame(dc, {7, 4, 17, 13}, RGB(193, 209, 233));
    Line(dc, 10, 5, 14, 10, RGB(14, 29, 52));
    Line(dc, 9, 10, 14, 7, RGB(184, 209, 245));
    Frame(dc, layout.shade, RGB(128, 145, 172));
    Frame(dc, layout.close, RGB(128, 145, 172));
    Line(dc, width - 27, 7, width - 21, 7, RGB(201, 208, 222));
    Line(dc, width - 27, 10, width - 21, 10, RGB(201, 208, 222));
    Line(dc, width - 15, 6, width - 10, 11, RGB(201, 208, 222));
    Line(dc, width - 15, 11, width - 10, 6, RGB(201, 208, 222));

    Gradient(dc, layout.menu, RGB(253, 253, 253), RGB(205, 207, 213));
    Line(dc, 3, 33, width - 3, 33, RGB(83, 88, 102));
    Text(dc, {7, 17, 33, 33}, L"File", 10, RGB(18, 20, 27));
    Text(dc, {39, 17, 82, 33}, L"Playlist", 10, RGB(18, 20, 27));
    Text(dc, {87, 17, 113, 33}, L"Sort", 10, RGB(18, 20, 27));
    Text(dc, {121, 17, 152, 33}, L"Help", 10, RGB(18, 20, 27));

    Frame(dc, {5, 37, width - 5, height - 24}, RGB(71, 74, 83));
    Frame(dc, {6, 38, width - 6, height - 25}, RGB(255, 255, 255));
    Fill(dc, layout.searchBorder, PlaylistBackground(theme));
    Frame(dc, layout.searchBorder, RGB(218, 222, 235));
    Line(dc, 8, 64, width - 8, 64, RGB(8, 24, 50));
    // Search glyph remains outside the actual editable HWND bounds.
    const HPEN searchPen = CreatePen(PS_SOLID, 2, RGB(239, 243, 255));
    const HGDIOBJ oldPen = SelectObject(dc, searchPen);
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, width - 25, 46, width - 18, 53);
    MoveToEx(dc, width - 25, 53, nullptr);
    LineTo(dc, width - 29, 57);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(searchPen);
    Fill(dc, layout.list, PlaylistBackground(theme));

    Gradient(dc, layout.footer, RGB(237, 238, 241), RGB(168, 172, 181));
    Line(dc, 4, height - 24, width - 4, height - 24, RGB(240, 241, 246));
    OvalButton(dc, layout.add, L"+ Add");
    OvalButton(dc, layout.remove, L"- Rem");
    OvalButton(dc, layout.select, L"= Sel");
    OvalButton(dc, layout.misc, L"* Misc");
    OvalButton(dc, layout.manage, L"Manage Playlist");
    const int timeSaved = SaveDC(dc);
    BeginPath(dc);
    RoundRect(dc, layout.time.left, layout.time.top,
        layout.time.right, layout.time.bottom, 12, 12);
    EndPath(dc);
    SelectClipPath(dc, RGN_AND);
    Gradient(dc, layout.time, RGB(58, 87, 139), RGB(8, 25, 64));
    RestoreDC(dc, timeSaved);
    Text(dc, layout.time, timeText.c_str(), 9, RGB(218, 230, 252), DT_CENTER);
    for (int offset = 0; offset < 8; offset += 3) {
        Line(dc, width - 6 - offset, height - 5, width - 5,
            height - 6 - offset, RGB(101, 107, 121));
    }
    RestoreDC(dc, saved);
}

}
