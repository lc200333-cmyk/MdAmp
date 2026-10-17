#include <windows.h>
#include <shellapi.h>

#include <array>
#include <string>

#include "presentation.h"

namespace {
constexpr wchar_t kWindowClass[] = L"MdAmp.MainWindow";
constexpr UINT kExitCommand = 1001;
constexpr UINT kAboutCommand = 1002;
constexpr std::array<const wchar_t*, 5> kTransportLabels = {
    L"Previous", L"Play", L"Pause", L"Stop", L"Next"};

struct WindowState {
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    std::array<HWND, kTransportLabels.size()> buttons{};
    bool ready = false;
};

void DrawTextAt(HDC dc, HFONT font, const wchar_t* text, RECT area, COLORREF color,
                UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    const auto oldFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &area, flags);
    SelectObject(dc, oldFont);
}

void PaintWindow(HWND window, const WindowState& state) {
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const auto background = CreateSolidBrush(RGB(24, 28, 36));
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);

    DrawTextAt(dc, state.titleFont, mdamp::kApplicationName,
               {24, 16, client.right - 24, 64}, RGB(92, 206, 214));
    DrawTextAt(dc, state.bodyFont, L"Independent Windows audio player / project scaffold",
               {24, 70, client.right - 24, 96}, RGB(208, 215, 225));
    DrawTextAt(dc, state.bodyFont, L"Audio playback is not implemented yet.",
               {24, 100, client.right - 24, 126}, RGB(236, 185, 100));
    const auto elapsed = mdamp::FormatDuration(0) + L" / " + mdamp::FormatDuration(0);
    DrawTextAt(dc, state.bodyFont, elapsed.c_str(),
               {24, 128, client.right - 24, 154}, RGB(208, 215, 225));

    const auto layout = mdamp::ComputeLayout(client.right, client.bottom);
    const auto& p = layout.playlist;
    RECT playlist{p.x, p.y, p.x + p.width, p.y + p.height};
    const auto panel = CreateSolidBrush(RGB(35, 41, 53));
    FillRect(dc, &playlist, panel);
    DeleteObject(panel);
    RECT caption{p.x + 12, p.y + 8, p.x + p.width - 12, p.y + p.height - 8};
    DrawTextAt(dc, state.bodyFont, L"PLAYLIST\n\nNo tracks. File opening will be added in the playback milestone.",
               caption, RGB(169, 183, 201), DT_LEFT | DT_WORDBREAK);
    DrawTextAt(dc, state.bodyFont, L"0.1.0-dev  |  Native C++ / Win32  |  Transport controls unavailable",
               {24, layout.statusY, client.right - 24, client.bottom}, RGB(145, 158, 177));
    EndPaint(window, &paint);
}

bool CreateControls(HWND window, WindowState& state) {
    state.titleFont = CreateFontW(-34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    state.bodyFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!state.titleFont || !state.bodyFont) return false;
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
    for (std::size_t i = 0; i < state.buttons.size(); ++i) {
        state.buttons[i] = CreateWindowExW(0, L"BUTTON", kTransportLabels[i],
            WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
            24 + static_cast<int>(i) * 96, 300, 88, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(2000 + i)), instance, nullptr);
        if (!state.buttons[i]) return false;
        SendMessageW(state.buttons[i], WM_SETFONT, reinterpret_cast<WPARAM>(state.bodyFont), TRUE);
    }
    return true;
}

void DestroyFonts(WindowState& state) {
    if (state.titleFont) DeleteObject(state.titleFont);
    if (state.bodyFont) DeleteObject(state.bodyFont);
    state.titleFont = nullptr;
    state.bodyFont = nullptr;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE:
        if (!state || !CreateControls(window, *state)) {
            if (state) DestroyFonts(*state);
            return -1;
        }
        state->ready = true;
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {mdamp::kMinimumWidth, mdamp::kMinimumHeight};
        return 0;
    }
    case WM_SIZE:
        if (state) {
            const auto layout = mdamp::ComputeLayout(LOWORD(lParam), HIWORD(lParam));
            for (std::size_t i = 0; i < state->buttons.size(); ++i) {
                if (state->buttons[i]) MoveWindow(state->buttons[i], 24 + static_cast<int>(i) * 96,
                                                 layout.buttonY, 88, 32, TRUE);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
        if (state) { PaintWindow(window, *state); return 0; }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kExitCommand: DestroyWindow(window); return 0;
        case kAboutCommand:
            MessageBoxW(window, L"MdAmp 0.1.0-dev\n\nIndependent C++ / Win32 project.\n"
                                L"This scaffold does not play audio yet.\nNo Winamp code, plugins or artwork are included.",
                        L"About MdAmp", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (state) { DestroyFonts(*state); state->ready = false; }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HMENU CreateMenus() {
    const auto menu = CreateMenu();
    const auto file = CreatePopupMenu();
    const auto view = CreatePopupMenu();
    const auto help = CreatePopupMenu();
    if (!menu || !file || !view || !help) {
        if (menu) DestroyMenu(menu);
        if (file) DestroyMenu(file);
        if (view) DestroyMenu(view);
        if (help) DestroyMenu(help);
        return nullptr;
    }
    bool ok = AppendMenuW(file, MF_STRING | MF_GRAYED, 1101, L"Open audio... (planned)") != FALSE;
    ok = (AppendMenuW(file, MF_SEPARATOR, 0, nullptr) != FALSE) && ok;
    ok = (AppendMenuW(file, MF_STRING, kExitCommand, L"Exit") != FALSE) && ok;
    ok = (AppendMenuW(view, MF_STRING | MF_GRAYED, 1102, L"Equalizer (planned)") != FALSE) && ok;
    ok = (AppendMenuW(view, MF_STRING | MF_GRAYED, 1103, L"Media library (planned)") != FALSE) && ok;
    ok = (AppendMenuW(help, MF_STRING, kAboutCommand, L"About MdAmp") != FALSE) && ok;
    if (!ok) { DestroyMenu(menu); DestroyMenu(file); DestroyMenu(view); DestroyMenu(help); return nullptr; }
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File")) {
        DestroyMenu(menu); DestroyMenu(file); DestroyMenu(view); DestroyMenu(help); return nullptr;
    }
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View")) {
        DestroyMenu(menu); DestroyMenu(view); DestroyMenu(help); return nullptr;
    }
    if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help")) {
        DestroyMenu(menu); DestroyMenu(help); return nullptr;
    }
    return menu;
}

bool HasSmokeTestArgument() {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool found = false;
    if (arguments) {
        for (int i = 1; i < count; ++i) {
            if (std::wstring(arguments[i]) == L"--smoke-test") found = true;
        }
        LocalFree(arguments);
    }
    return found;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const bool smokeTest = HasSmokeTestArgument();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) return 1;
    const auto menu = CreateMenus();
    if (!menu) return 2;
    WindowState state;
    const auto window = CreateWindowExW(0, kWindowClass, mdamp::kWindowTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 880, 540, nullptr, menu, instance, &state);
    if (!window) { if (IsMenu(menu)) DestroyMenu(menu); return 3; }
    if (smokeTest) {
        bool ok = state.ready && GetMenuItemCount(menu) == 3;
        wchar_t title[128]{};
        GetWindowTextW(window, title, 128);
        ok = ok && std::wstring(title) == mdamp::kWindowTitle;
        for (const auto button : state.buttons) ok = ok && IsWindow(button) && !IsWindowEnabled(button);
        SendMessageW(window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(880, 480));
        DestroyWindow(window);
        UnregisterClassW(kWindowClass, instance);
        return ok ? 0 : 4;
    }
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (true) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) return 5;
        if (result == 0) return static_cast<int>(message.wParam);
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
