#include <iostream>
#include <limits>
#include "presentation.h"

int main() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    check(mdamp::FormatDuration(0) == L"0:00", "zero duration");
    check(mdamp::FormatDuration(9) == L"0:09", "seconds zero padding");
    check(mdamp::FormatDuration(59) == L"0:59", "minute boundary below");
    check(mdamp::FormatDuration(60) == L"1:00", "minute boundary");
    check(mdamp::FormatDuration(3601) == L"60:01", "long duration");
    check(mdamp::FormatDuration((std::numeric_limits<std::uint64_t>::max)()) ==
          L"307445734561825860:15", "no duration integer overflow");
    const auto normal = mdamp::ComputeLayout(880, 480);
    check(normal.playlist.x == 24 && normal.playlist.width == 832, "playlist horizontal margins");
    check(normal.playlist.y + normal.playlist.height <= normal.buttonY, "playlist above transport");
    check(normal.buttonY + 32 <= normal.statusY, "transport above status");
    for (const auto width : {-1, 0, 20, 760, 1920}) {
        for (const auto height : {-1, 0, 20, 460, 1080}) {
            const auto layout = mdamp::ComputeLayout(width, height);
            check(layout.playlist.width >= 0 && layout.playlist.height >= 0,
                  "nonnegative panel dimensions during resize");
            check(layout.buttonY >= 0 && layout.statusY >= 0, "nonnegative control anchors");
        }
    }
    if (failures) return 1;
    std::cout << "All presentation checks passed.\n";
    return 0;
}
