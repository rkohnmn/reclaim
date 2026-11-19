#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "main_window.hpp"

namespace {

class GdiplusGuard {
public:
    GdiplusGuard() {
        Gdiplus::GdiplusStartupInput input;
        ok_ = (Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok);
    }
    ~GdiplusGuard() {
        if (ok_) Gdiplus::GdiplusShutdown(token_);
    }
    bool ok() const noexcept { return ok_; }

private:
    ULONG_PTR token_{0};
    bool ok_{false};
};

std::filesystem::path parse_initial_scan_arg() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};

    std::filesystem::path target;
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], L"--scan") == 0) {
            target = std::filesystem::path(argv[i + 1]);
            break;
        }
    }
    LocalFree(argv);
    return target;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    GdiplusGuard gdip;
    if (!gdip.ok()) {
        MessageBoxW(nullptr, L"Failed to initialize GDI+ graphics subsystem.", L"Reclaim Error", MB_ICONERROR);
        return 1;
    }

    reclaim::ui::MainWindow window;
    const auto cli_scan_path = parse_initial_scan_arg();
    if (!cli_scan_path.empty()) {
        window.set_initial_scan_path(cli_scan_path);
    }
    if (!window.create(instance, show_cmd)) {
        MessageBoxW(nullptr, L"Failed to create Reclaim application window.", L"Reclaim Error", MB_ICONERROR);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

#endif  // _WIN32
