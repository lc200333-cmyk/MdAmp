#include "modern_ui.h"
#include "playlist_ui.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool Inside(const RECT& rect, int width, int height) {
    return rect.left >= 0 && rect.top >= 0 && rect.right <= width &&
        rect.bottom <= height && rect.left < rect.right && rect.top < rect.bottom;
}

bool Overlaps(const RECT& first, const RECT& second) {
    return first.left < second.right && second.left < first.right &&
        first.top < second.bottom && second.top < first.bottom;
}

struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;

    Surface(int requestedWidth, int requestedHeight)
        : width(requestedWidth), height(requestedHeight) {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("CreateCompatibleDC failed");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(dc);
            throw std::runtime_error("CreateDIBSection failed");
        }
        pixels = static_cast<std::uint32_t*>(data);
        oldBitmap = SelectObject(dc, bitmap);
    }
    ~Surface() {
        if (dc) {
            SelectObject(dc, oldBitmap);
            DeleteDC(dc);
        }
        if (bitmap) DeleteObject(bitmap);
    }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
};

constexpr std::uint32_t sentinel = 0x7f123456U;

template<class Painter>
std::vector<std::uint32_t> RenderWithGuard(int width, int height, Painter paint) {
    Surface surface(width + 20, height + 20);
    const std::size_t count = static_cast<std::size_t>(surface.width) * surface.height;
    std::fill_n(surface.pixels, count, sentinel);
    const int saved = SaveDC(surface.dc);
    SetViewportOrgEx(surface.dc, 10, 10, nullptr);
    IntersectClipRect(surface.dc, 0, 0, width, height);
    paint(surface.dc);
    RestoreDC(surface.dc, saved);
    GdiFlush();
    bool changed = false;
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) {
            const auto pixel = surface.pixels[static_cast<std::size_t>(y) * surface.width + x];
            if (x < 10 || x >= width + 10 || y < 10 || y >= height + 10)
                Require(pixel == sentinel, "Painter wrote outside the caller's clip rectangle");
            else changed = changed || pixel != sentinel;
        }
    }
    Require(changed, "Painter did not draw any pixels");
    return {surface.pixels, surface.pixels + count};
}

void PlayerGeometry() {
    Require(mdamp::PlayerLogicalWidth(-1) == 354, "Negative player width is not clamped");
    Require(mdamp::PlayerLogicalWidth(100000) == 4096, "Oversized player width is not clamped");
    const auto menus = mdamp::PlayerMenuRects();
    for (std::size_t i = 0; i < menus.size(); ++i) {
        Require(Inside(menus[i], 354, 164), "Player menu rectangle out of bounds");
        Require(menus[i].top == 17 && menus[i].bottom == 33, "Player menu vertical geometry changed");
        for (std::size_t j = 0; j < i; ++j)
            Require(!Overlaps(menus[i], menus[j]), "Player menu regions overlap");
    }
    for (int width : {354, 487, 800}) {
        for (bool shade : {false, true}) {
            for (bool drawer : {false, true}) {
                for (int tab = 0; tab < 3; ++tab) {
                    const int height = mdamp::PlayerLogicalHeight(shade, drawer);
                    const auto regions = mdamp::PlayerHits(shade, drawer, tab, width);
                    Require(!regions.empty(), "Player has no hit regions");
                    for (std::size_t i = 0; i < regions.size(); ++i) {
                        Require(Inside(regions[i].rect, width, height), "Player hit rectangle out of bounds");
                        Require(regions[i].action != mdamp::Action::None, "Player hit has no action");
                        for (std::size_t j = 0; j < i; ++j)
                            Require(!Overlaps(regions[i].rect, regions[j].rect), "Player hit rectangles overlap");
                    }
                    const auto hasAction = [&regions](mdamp::Action action) {
                        return std::any_of(regions.begin(), regions.end(), [action](const auto& region) {
                            return region.action == action;
                        });
                    };
                    Require(hasAction(mdamp::Action::Close), "Player close action missing");
                    Require(hasAction(mdamp::Action::Shade), "Player shade action missing");
                    if (!shade) {
                        Require(hasAction(mdamp::Action::Play), "Player transport missing");
                        Require(hasAction(mdamp::Action::Seek), "Player seek action missing");
                    }
                    if (shade || !drawer || tab != 0)
                        Require(!hasAction(mdamp::Action::Band0), "Equalizer hit remains active while hidden");
                    else Require(hasAction(mdamp::Action::Band9), "Equalizer band action missing");
                }
            }
        }
    }
}

void PlaylistGeometry() {
    for (int width : {354, 487, 800}) {
        for (int height : {164, 280, 976}) {
            const auto layout = mdamp::ComputePlaylistLayout(width, height);
            const RECT areas[]{layout.title, layout.menu, layout.searchBorder,
                layout.query, layout.list, layout.footer, layout.add, layout.remove,
                layout.select, layout.misc, layout.time, layout.manage, layout.close, layout.shade};
            for (const RECT& area : areas)
                Require(Inside(area, width, height), "Playlist rectangle out of bounds");
            Require(!Overlaps(layout.query, layout.list), "Search overlaps media rows");
            Require(!Overlaps(layout.list, layout.footer), "Media rows overlap footer");
            const RECT footer[]{layout.add, layout.remove, layout.select, layout.misc,
                layout.time, layout.manage};
            for (std::size_t i = 0; i < 6; ++i) {
                for (std::size_t j = 0; j < i; ++j)
                    Require(!Overlaps(footer[i], footer[j]), "Playlist footer regions overlap");
            }
            const mdamp::PlaylistFooterAction actions[]{mdamp::PlaylistFooterAction::Add,
                mdamp::PlaylistFooterAction::Remove, mdamp::PlaylistFooterAction::Select,
                mdamp::PlaylistFooterAction::Misc, mdamp::PlaylistFooterAction::None,
                mdamp::PlaylistFooterAction::Manage};
            for (std::size_t i = 0; i < 6; ++i) {
                const int x = static_cast<int>((footer[i].left + footer[i].right) / 2);
                const int y = static_cast<int>((footer[i].top + footer[i].bottom) / 2);
                Require(mdamp::PlaylistFooterHit(x, y, width, height) == actions[i],
                    "Playlist footer painter and pointer map disagree");
            }
            Require(mdamp::PlaylistFooterHit(-1, -1, width, height) ==
                mdamp::PlaylistFooterAction::None, "Negative pointer activates footer");
            Require(mdamp::PlaylistFooterHit(width, height, width, height) ==
                mdamp::PlaylistFooterAction::None, "Outside pointer activates footer");
        }
    }
    const auto minimum = mdamp::ComputePlaylistLayout(0, 0);
    const auto clamped = mdamp::ComputePlaylistLayout(354, 164);
    Require(EqualRect(&minimum.list, &clamped.list) != FALSE, "Playlist dimensions are not clamped");
    Require(mdamp::PlaylistBackground(0) == RGB(28, 61, 125), "Observed blue playlist palette changed");
}

void Rendering() {
    for (int width : {354, 487, 800}) {
        for (int theme = 0; theme < 6; ++theme) {
            for (int tab = 0; tab < 3; ++tab) {
                mdamp::PlayerView view;
                view.width = width;
                view.theme = theme;
                view.tab = tab;
                view.title = L"Synthetic media";
                view.time = L"00:00";
                view.progress = .4f;
                view.eq = {0, 1, -2, 3, -4, 5, -6, 7, -8, 9};
                for (bool shade : {false, true}) {
                    for (bool drawer : {false, true}) {
                        view.shade = shade;
                        view.drawer = drawer;
                        const int height = mdamp::PlayerLogicalHeight(shade, drawer);
                        const auto painter = [&view](HDC dc) { mdamp::PaintPlayer(dc, view); };
                        const auto first = RenderWithGuard(width, height, painter);
                        const auto second = RenderWithGuard(width, height, painter);
                        Require(first == second, "Same player input produces unstable pixels");
                    }
                }
            }
            const auto painter = [width, theme](HDC dc) {
                mdamp::PaintPlaylistChrome(dc, width, 976, theme, L"0:00/0:02");
            };
            Require(RenderWithGuard(width, 976, painter) == RenderWithGuard(width, 976, painter),
                "Same playlist input produces unstable pixels");
        }
    }
}

template<class Painter>
std::vector<std::uint32_t> RenderTransformed(int width, int height,
    int scale, int offset, Painter painter) {
    const int pixelWidth = width * scale, pixelHeight = height * scale;
    Surface surface(pixelWidth + offset + 13, pixelHeight + offset + 13);
    const std::size_t count = static_cast<std::size_t>(surface.width) * surface.height;
    std::fill_n(surface.pixels, count, sentinel);
    const int saved = SaveDC(surface.dc);
    SetMapMode(surface.dc, MM_ANISOTROPIC);
    SetWindowExtEx(surface.dc, width, height, nullptr);
    SetViewportExtEx(surface.dc, pixelWidth, pixelHeight, nullptr);
    SetViewportOrgEx(surface.dc, offset, offset, nullptr);
    IntersectClipRect(surface.dc, 0, 0, width, height);
    painter(surface.dc);
    RestoreDC(surface.dc, saved);
    GdiFlush();
    std::vector<std::uint32_t> result;
    result.reserve(static_cast<std::size_t>(pixelWidth) * pixelHeight);
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) {
            const auto pixel = surface.pixels[static_cast<std::size_t>(y) * surface.width + x];
            if (x < offset || x >= offset + pixelWidth || y < offset || y >= offset + pixelHeight)
                Require(pixel == sentinel, "Transformed painter wrote outside the caller clip");
            else result.push_back(pixel);
        }
    }
    return result;
}

void TranslatedAndScaledRendering() {
    constexpr int width = 487, height = 976;
    const auto playlistPainter = [](HDC dc) {
        mdamp::PaintPlaylistChrome(dc, width, height, 0, L"0:00/0:02");
    };
    mdamp::PlayerView view;
    view.width = width;
    view.title = L"Synthetic translated fixture";
    const auto playerPainter = [&view](HDC dc) { mdamp::PaintPlayer(dc, view); };
    for (int scale : {1, 2}) {
        const auto origin = RenderTransformed(width, height, scale, 0, playlistPainter);
        const auto translated = RenderTransformed(width, height, scale, 17, playlistPainter);
        Require(origin == translated, "Playlist pixels depend on viewport translation");
        const auto layout = mdamp::ComputePlaylistLayout(width, height);
        const int x = static_cast<int>(layout.time.left + 15) * scale;
        const int y = static_cast<int>(layout.time.top + 3) * scale;
        const auto timePixel = translated[static_cast<std::size_t>(y) * width * scale + x];
        const auto blue = timePixel & 255U, red = (timePixel >> 16) & 255U;
        Require(blue > red + 20, "Scaled time capsule lost its navy gradient fill");
        Require(RenderTransformed(width, 280, scale, 0, playerPainter) ==
            RenderTransformed(width, 280, scale, 17, playerPainter),
            "Player pixels depend on viewport translation");
    }
}
}

int main() {
    try {
        PlayerGeometry();
        PlaylistGeometry();
        Rendering();
        TranslatedAndScaledRendering();
        std::cout << "Layout, pointer geometry and deterministic own-renderer checks passed.\n"
            "These are synthetic tests, not Winamp screenshot parity evidence.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Modern layout test failed: " << error.what() << '\n';
        return 1;
    }
}
