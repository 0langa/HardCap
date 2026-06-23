#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include "core/rule_repository.h"
#include "platform/win32_handle.h"
#include "ui/main_window.h"

namespace {
hardcap::UniqueHandle single_instance;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, const int show_command) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    single_instance.reset(CreateMutexW(nullptr, TRUE, L"Local\\HardCap.SingleInstance"));
    if (!single_instance || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"HardCap.MainWindow", nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        MessageBoxW(nullptr,
                    L"HardCap is already running, so this launch brought the existing instance forward.\n\n"
                    L"If you just rebuilt or replaced HardCap.exe, choose Exit from the tray menu first, then launch the updated exe again.",
                    L"HardCap is already running",
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    hardcap::MainWindow window(instance, hardcap::RuleRepository(hardcap::RuleRepository::default_path()));
    const int result = window.run(show_command);

    if (SUCCEEDED(com)) CoUninitialize();
    ReleaseMutex(single_instance.get());
    return result;
}
