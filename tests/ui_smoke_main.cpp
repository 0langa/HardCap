#include "core/rule_repository.h"
#include "ui/main_window.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <filesystem>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, const int show_command) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto settings = std::filesystem::temp_directory_path() / L"hardcap-ui-smoke.json";
    hardcap::MainWindow window(instance, hardcap::RuleRepository(settings));
    const int result = window.run(show_command);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
