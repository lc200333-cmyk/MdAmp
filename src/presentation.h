#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace mdamp {
inline constexpr wchar_t kApplicationName[] = L"MdAmp";
inline constexpr wchar_t kWindowTitle[] = L"MdAmp";
inline constexpr int kMinimumWidth = 760;
inline constexpr int kMinimumHeight = 460;

struct Rect {
    int x;
    int y;
    int width;
    int height;
};

struct Layout {
    Rect playlist;
    int buttonY;
    int statusY;
};

inline Layout ComputeLayout(int width, int height) {
    const int safeWidth = std::max(0, width);
    const int safeHeight = std::max(0, height);
    const int statusY = std::max(0, safeHeight - 34);
    const int buttonY = std::max(0, statusY - 46);
    return {{24, 158, std::max(0, safeWidth - 48),
             std::max(0, buttonY - 178)}, buttonY, statusY};
}

inline std::wstring FormatDuration(std::uint64_t seconds) {
    const auto minutes = seconds / 60;
    const auto remainder = seconds % 60;
    return std::to_wstring(minutes) + L":" +
           (remainder < 10 ? L"0" : L"") + std::to_wstring(remainder);
}
} // namespace mdamp
