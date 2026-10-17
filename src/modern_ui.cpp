#include "modern_ui.h"
#include <algorithm>
#include <cmath>

namespace mdamp {
namespace {
constexpr RECT titleMin{297, 5, 312, 20};
constexpr RECT titleShade{314, 5, 329, 20};
constexpr RECT titleClose{331, 5, 348, 20};
constexpr RECT seek{14, 97, 338, 109};
constexpr RECT volume{237, 115, 338, 127};
constexpr RECT pan{15, 191, 74, 202};
constexpr RECT preamp{88, 185, 101, 258};

COLORREF Blend(COLORREF a, COLORREF b, int weight) {
    const auto channel = [weight](int first, int second) {
        return (first * (255 - weight) + second * weight) / 255;
    };
    return RGB(channel(GetRValue(a), GetRValue(b)),
        channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
}
void Fill(HDC dc, RECT rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    const HPEN pen = CreatePen(PS_SOLID, width, color);
    const HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}
void Border(HDC dc, RECT rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}
void Gradient(HDC dc, RECT rect, COLORREF top, COLORREF bottom) {
    const int height = static_cast<int>(rect.bottom - rect.top);
    for (int y = 0; y < height; ++y) {
        const RECT row{rect.left, rect.top + y, rect.right, rect.top + y + 1};
        Fill(dc, row, Blend(top, bottom, y * 255 / std::max(1, height - 1)));
    }
}
void Label(HDC dc, RECT rect, const wchar_t* text, COLORREF color,
    int size = 10, UINT align = DT_LEFT, bool bold = false, const wchar_t* face = L"Segoe UI") {
    const HFONT font = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    const HGDIOBJ old = SelectObject(dc, font);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, old);
    DeleteObject(font);
}
void Button(HDC dc, RECT rect, const wchar_t* text, const Palette& p, bool selected = false,
    int fontSize = 9) {
    const COLORREF base = selected ? Blend(p.panel, p.accent, 80) : p.panel;
    Gradient(dc, rect, Blend(base, RGB(255, 255, 255), 90), Blend(base, RGB(0, 0, 0), 30));
    Border(dc, rect, Blend(p.dim, RGB(0, 0, 0), 70));
    const RECT inner{rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
    Line(dc, static_cast<int>(inner.left), static_cast<int>(inner.top),
        static_cast<int>(inner.right), static_cast<int>(inner.top), Blend(base, RGB(255, 255, 255), 170));
    Label(dc, rect, text, selected ? p.accent : p.text, fontSize, DT_CENTER, selected);
}
void Glyph(HDC dc, RECT rect, Action action, const Palette& p, bool selected = false) {
    Button(dc, rect, L"", p, selected);
    const int cx = static_cast<int>((rect.left + rect.right) / 2);
    const int cy = static_cast<int>((rect.top + rect.bottom) / 2);
    const COLORREF ink = selected ? p.accent : p.text;
    if (action == Action::Pause) {
        Fill(dc, {cx - 5, cy - 5, cx - 2, cy + 5}, ink);
        Fill(dc, {cx + 2, cy - 5, cx + 5, cy + 5}, ink);
    } else if (action == Action::Stop) {
        Fill(dc, {cx - 4, cy - 4, cx + 4, cy + 4}, ink);
    } else {
        POINT points[3]{};
        if (action == Action::Previous) {
            points[0] = {cx + 4, cy - 5}; points[1] = {cx + 4, cy + 5}; points[2] = {cx - 3, cy};
            Fill(dc, {cx - 6, cy - 5, cx - 4, cy + 5}, ink);
        } else if (action == Action::Open) {
            points[0] = {cx, cy - 5}; points[1] = {cx - 5, cy + 1}; points[2] = {cx + 5, cy + 1};
            Fill(dc, {cx - 5, cy + 3, cx + 6, cy + 5}, ink);
        } else {
            points[0] = {cx - 4, cy - 5}; points[1] = {cx - 4, cy + 5}; points[2] = {cx + 4, cy};
            if (action == Action::Next) Fill(dc, {cx + 5, cy - 5, cx + 7, cy + 5}, ink);
        }
        const HBRUSH brush = CreateSolidBrush(ink);
        const HPEN pen = CreatePen(PS_SOLID, 1, ink);
        const HGDIOBJ oldBrush = SelectObject(dc, brush);
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        Polygon(dc, points, 3);
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
        DeleteObject(pen); DeleteObject(brush);
    }
}
void Horizontal(HDC dc, RECT rect, float value, const Palette& p, bool centered = false) {
    const int left = static_cast<int>(rect.left);
    const int right = static_cast<int>(rect.right);
    const int center = static_cast<int>((rect.top + rect.bottom) / 2);
    Fill(dc, {left, center - 2, right, center + 2}, Blend(p.dim, RGB(0, 0, 0), 90));
    const float position = centered ? (value + 1.0f) * .5f : value;
    const int x = left + static_cast<int>(std::clamp(position, 0.0f, 1.0f) * static_cast<float>(right - left - 7));
    if (!centered) Fill(dc, {left + 1, center - 1, x + 3, center + 1}, p.accent);
    else Line(dc, (left + right) / 2, center - 4, (left + right) / 2, center + 4, p.dim);
    Gradient(dc, {x, center - 5, x + 7, center + 5}, RGB(248, 249, 252), p.panel);
    Border(dc, {x, center - 5, x + 7, center + 5}, p.dim);
}
void Vertical(HDC dc, RECT rect, float db, const Palette& p) {
    const int x = static_cast<int>((rect.left + rect.right) / 2);
    const int top = static_cast<int>(rect.top);
    const int bottom = static_cast<int>(rect.bottom);
    Fill(dc, {x - 2, top, x + 2, bottom}, Blend(p.dim, RGB(0, 0, 0), 70));
    Line(dc, x - 4, (top + bottom) / 2, x + 4, (top + bottom) / 2, p.dim);
    const int y = top + static_cast<int>((12.0f - std::clamp(db, -12.0f, 12.0f)) / 24.0f * static_cast<float>(bottom - top - 8));
    Gradient(dc, {x - 6, y, x + 7, y + 8}, Blend(p.panel, RGB(255, 255, 255), 160), p.panel);
    Border(dc, {x - 6, y, x + 7, y + 8}, p.dim);
    Line(dc, x - 3, y + 4, x + 4, y + 4, p.accent);
}
RECT EqRect(int band) { const int x = 135 + band * 19; return {x, 185, x + 13, 258}; }
void Add(std::vector<HitRegion>& hits, RECT rect, Action action) { hits.push_back({rect, action}); }
}

Palette ThemePalette(int theme) {
    static constexpr Palette palettes[]{
        {RGB(175, 185, 202), RGB(206, 213, 225), RGB(24, 34, 58), RGB(38, 100, 212), RGB(99, 114, 139)},
        {RGB(35, 40, 49), RGB(63, 72, 85), RGB(228, 234, 243), RGB(74, 180, 255), RGB(104, 119, 136)},
        {RGB(56, 62, 50), RGB(107, 119, 90), RGB(239, 244, 213), RGB(195, 231, 80), RGB(78, 88, 64)},
        {RGB(72, 49, 43), RGB(136, 101, 81), RGB(255, 239, 215), RGB(255, 164, 60), RGB(103, 74, 63)},
        {RGB(62, 51, 80), RGB(110, 94, 140), RGB(246, 233, 255), RGB(221, 154, 255), RGB(88, 75, 107)},
        {RGB(183, 195, 192), RGB(214, 226, 221), RGB(27, 58, 51), RGB(17, 132, 109), RGB(103, 133, 124)}
    };
    const int count = static_cast<int>(std::size(palettes));
    return palettes[((theme % count) + count) % count];
}
std::vector<std::wstring> ThemeNames() {
    return {L"Modern Silver", L"Midnight Blue", L"Phosphor", L"Amber", L"Violet", L"Sea Glass"};
}
std::vector<HitRegion> PlayerHits(bool shade, bool drawer, int tab) {
    std::vector<HitRegion> hits;
    Add(hits, titleMin, Action::Minimize); Add(hits, titleShade, Action::Shade); Add(hits, titleClose, Action::Close);
    if (shade) {
        Add(hits, {8, 4, 26, 21}, Action::Previous); Add(hits, {27, 4, 45, 21}, Action::Play);
        Add(hits, {46, 4, 64, 21}, Action::Pause); Add(hits, {65, 4, 83, 21}, Action::Stop);
        Add(hits, {84, 4, 102, 21}, Action::Next); Add(hits, {249, 5, 291, 20}, Action::Volume);
        return hits;
    }
    constexpr Action transport[]{Action::Previous, Action::Play, Action::Pause, Action::Stop, Action::Next, Action::Open};
    for (int i = 0; i < 6; ++i) Add(hits, {14 + i * 31, 113, 43 + i * 31, 133}, transport[i]);
    Add(hits, seek, Action::Seek); Add(hits, volume, Action::Volume);
    Add(hits, {237, 130, 286, 144}, Action::Shuffle); Add(hits, {289, 130, 338, 144}, Action::Repeat);
    Add(hits, {251, 74, 290, 90}, Action::TogglePlaylist); Add(hits, {294, 74, 333, 90}, Action::ToggleLibrary);
    Add(hits, {16, 75, 116, 91}, Action::Visualize);
    Add(hits, {328, 146, 348, 161}, Action::ToggleDrawer);
    Add(hits, {10, 146, 111, 161}, Action::DrawerEq); Add(hits, {113, 146, 214, 161}, Action::DrawerOptions);
    Add(hits, {216, 146, 326, 161}, Action::DrawerThemes);
    if (!drawer) return hits;
    if (tab == 0) {
        Add(hits, pan, Action::Pan); Add(hits, preamp, Action::Preamp);
        Add(hits, {15, 212, 72, 226}, Action::EqToggle); Add(hits, {15, 229, 72, 243}, Action::EqAuto);
        Add(hits, {15, 246, 72, 260}, Action::EqPreset);
        for (int band = 0; band < 10; ++band)
            Add(hits, EqRect(band), static_cast<Action>(static_cast<int>(Action::Band0) + band));
    } else if (tab == 1) {
        Add(hits, {20, 193, 174, 216}, Action::Preferences); Add(hits, {181, 193, 334, 216}, Action::TogglePlaylist);
        Add(hits, {20, 224, 174, 247}, Action::Shuffle); Add(hits, {181, 224, 334, 247}, Action::Repeat);
    } else {
        Add(hits, {18, 191, 332, 226}, Action::ThemeSelect);
        Add(hits, {182, 239, 226, 260}, Action::ThemePrevious); Add(hits, {231, 239, 275, 260}, Action::ThemeNext);
        Add(hits, {280, 239, 333, 260}, Action::ThemeSelect);
    }
    return hits;
}

void PaintFrame(HDC dc, RECT bounds, const wchar_t* title, int theme) {
    const Palette p = ThemePalette(theme);
    Gradient(dc, bounds, Blend(p.panel, RGB(255, 255, 255), 50), p.background);
    Border(dc, bounds, Blend(p.dim, RGB(0, 0, 0), 120));
    const RECT inner{bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1};
    Border(dc, inner, Blend(p.panel, RGB(255, 255, 255), 140));
    const RECT bar{bounds.left + 3, bounds.top + 3, bounds.right - 3, bounds.top + 24};
    Gradient(dc, bar, Blend(p.panel, RGB(255, 255, 255), 115), p.background);
    Label(dc, {bar.left + 8, bar.top, bounds.right - 26, bar.bottom}, title, p.text, 11, DT_LEFT, true);
    Label(dc, {bounds.right - 22, bounds.top + 4, bounds.right - 3, bounds.top + 20}, L"x", p.text, 11, DT_CENTER, true);
    Line(dc, static_cast<int>(bounds.left + 4), static_cast<int>(bounds.top + 24),
        static_cast<int>(bounds.right - 4), static_cast<int>(bounds.top + 24), p.dim);
}

void PaintPlayer(HDC dc, const PlayerView& view) {
    const Palette p = ThemePalette(view.theme);
    PaintFrame(dc, {0, 0, 354, view.shade ? 25 : (view.drawer ? 280 : 164)}, view.shade ? L"" : L"MdAmp", view.theme);
    if (view.shade) {
        constexpr Action actions[]{Action::Previous, Action::Play, Action::Pause, Action::Stop, Action::Next};
        for (int i = 0; i < 5; ++i) Glyph(dc, {8 + i * 19, 4, 26 + i * 19, 21}, actions[i], p,
            (i == 1 && view.playing && !view.paused) || (i == 2 && view.paused));
        Label(dc, {107, 4, 244, 21}, view.title.c_str(), p.text, 10);
        Horizontal(dc, {249, 5, 291, 20}, view.volume, p);
    } else {
        Label(dc, {8, 25, 45, 39}, L"File", p.text, 10);
        Label(dc, {45, 25, 83, 39}, L"Play", p.text, 10);
        Label(dc, {83, 25, 139, 39}, L"Options", p.text, 10);
        Label(dc, {139, 25, 181, 39}, L"View", p.text, 10);
        Label(dc, {181, 25, 230, 39}, L"Help", p.text, 10);
        const COLORREF display = Blend(p.background, RGB(4, 13, 34), 240);
        Fill(dc, {13, 42, 340, 93}, display);
        Border(dc, {13, 42, 340, 93}, Blend(p.dim, RGB(0, 0, 0), 160));
        Line(dc, 14, 92, 340, 92, Blend(p.panel, RGB(255, 255, 255), 145));
        const COLORREF displayInk = Blend(p.accent, RGB(211, 235, 255), 180);
        Label(dc, {20, 45, 119, 77}, view.time.c_str(), displayInk, 25, DT_RIGHT, false, L"Consolas");
        Label(dc, {133, 44, 332, 59}, view.title.c_str(), displayInk, 11, DT_LEFT, true);
        Label(dc, {133, 60, 332, 73}, view.info.c_str(), Blend(displayInk, display, 55), 9);
        Label(dc, {122, 74, 246, 90}, view.paused ? L"PAUSED" : (view.playing ? L"PLAYING" : L"STOPPED"), displayInk, 9);
        for (int i = 0; i < 32; ++i) {
            const float level = std::clamp(view.spectrum[static_cast<size_t>(i)], 0.0f, 1.0f);
            const int height = static_cast<int>(level * 16.0f);
            Fill(dc, {19 + i * 3, 90 - height, 21 + i * 3, 90}, Blend(p.accent, RGB(128, 229, 255), 100));
        }
        Button(dc, {251, 74, 290, 90}, L"PL", p); Button(dc, {294, 74, 333, 90}, L"ML", p);
        Horizontal(dc, seek, view.progress, p);
        constexpr Action actions[]{Action::Previous, Action::Play, Action::Pause, Action::Stop, Action::Next, Action::Open};
        for (int i = 0; i < 6; ++i) Glyph(dc, {14 + i * 31, 113, 43 + i * 31, 133}, actions[i], p,
            (i == 1 && view.playing && !view.paused) || (i == 2 && view.paused));
        Label(dc, {205, 114, 235, 127}, L"VOL", p.text, 8, DT_CENTER);
        Horizontal(dc, volume, view.volume, p);
        Button(dc, {237, 130, 286, 144}, L"SHUFFLE", p, view.shuffle, 8);
        Button(dc, {289, 130, 338, 144}, view.repeat < 0 ? L"REPEAT 1" : L"REPEAT", p, view.repeat != 0, 8);
        Button(dc, {10, 146, 111, 161}, L"EQUALIZER", p, view.drawer && view.tab == 0, 9);
        Button(dc, {113, 146, 214, 161}, L"OPTIONS", p, view.drawer && view.tab == 1, 9);
        Button(dc, {216, 146, 326, 161}, L"COLOR THEMES", p, view.drawer && view.tab == 2, 9);
        Button(dc, {328, 146, 348, 161}, view.drawer ? L"-" : L"+", p);
        if (view.drawer) {
            Fill(dc, {10, 178, 344, 273}, Blend(p.background, p.panel, 110));
            Border(dc, {10, 178, 344, 273}, p.dim);
            if (view.tab == 0) {
                Label(dc, {15, 179, 75, 190}, L"BALANCE", p.text, 8, DT_CENTER);
                Horizontal(dc, pan, view.pan, p, true);
                Button(dc, {15, 212, 72, 226}, L"ON", p, view.eqEnabled, 8);
                Button(dc, {15, 229, 72, 243}, L"FLAT", p, false, 8);
                Button(dc, {15, 246, 72, 260}, L"PRESETS", p, false, 8);
                Label(dc, {80, 261, 111, 272}, L"PRE", p.text, 8, DT_CENTER);
                Label(dc, {106, 180, 130, 193}, L"+12", p.dim, 8);
                Label(dc, {111, 216, 129, 230}, L"0", p.dim, 8);
                Label(dc, {106, 249, 130, 261}, L"-12", p.dim, 8);
                Vertical(dc, preamp, view.preamp, p);
                constexpr const wchar_t* frequencies[]{L"60", L"170", L"310", L"600", L"1k", L"3k", L"6k", L"12k", L"14k", L"16k"};
                for (int band = 0; band < 10; ++band) {
                    Vertical(dc, EqRect(band), view.eq[static_cast<size_t>(band)], p);
                    Label(dc, {132 + band * 19, 260, 153 + band * 19, 272}, frequencies[band], p.text, 8, DT_CENTER);
                }
            } else if (view.tab == 1) {
                Button(dc, {20, 193, 174, 216}, L"Preferences...  Ctrl+P", p, false, 10);
                Button(dc, {181, 193, 334, 216}, L"Playlist Editor  Alt+E", p, false, 10);
                Button(dc, {20, 224, 174, 247}, L"Shuffle  S", p, view.shuffle, 10);
                Button(dc, {181, 224, 334, 247}, L"Repeat  R", p, view.repeat != 0, 10);
                Label(dc, {20, 251, 334, 267}, L"Playback, audio, skins and plug-ins in Preferences", p.dim, 9, DT_CENTER);
            } else {
                const auto names = ThemeNames();
                const size_t index = static_cast<size_t>(((view.theme % 6) + 6) % 6);
                Button(dc, {18, 191, 332, 226}, names[index].c_str(), p, true, 15);
                Label(dc, {22, 239, 175, 260}, L"MdAmp original palettes", p.dim, 9);
                Button(dc, {182, 239, 226, 260}, L"<", p);
                Button(dc, {231, 239, 275, 260}, L">", p);
                Button(dc, {280, 239, 333, 260}, L"Select", p);
            }
        }
    }
    Button(dc, titleMin, L"_", p, false, 10);
    Button(dc, titleShade, view.shade ? L"+" : L"=", p, false, 10);
    Button(dc, titleClose, L"x", p, false, 10);
}
}
