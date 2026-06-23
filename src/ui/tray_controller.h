#pragma once

#include <windows.h>
#include <shellapi.h>

namespace hardcap {

class TrayController {
public:
    static constexpr UINT callback_message = WM_APP + 42;
    static constexpr UINT command_show = 41001;
    static constexpr UINT command_pause = 41002;
    static constexpr UINT command_exit = 41003;

    ~TrayController();
    bool create(HWND owner, HICON icon);
    void remove();
    void show_menu(bool paused) const;

private:
    NOTIFYICONDATAW data_{};
    bool added_{false};
};

} // namespace hardcap
