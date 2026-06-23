#include "ui/tray_controller.h"

namespace hardcap {

TrayController::~TrayController() { remove(); }

bool TrayController::create(HWND owner, HICON icon) {
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = callback_message;
    data_.hIcon = icon;
    wcscpy_s(data_.szTip, L"HardCap — resource limits active while running");
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

void TrayController::remove() {
    if (added_) Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayController::show_menu(const bool paused) const {
    if (!added_) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, command_show, L"Show HardCap");
    AppendMenuW(menu, MF_STRING, command_pause, paused ? L"Resume all limits" : L"Pause all limits");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, command_exit, L"Exit and remove limits");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(data_.hWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   point.x, point.y, 0, data_.hWnd, nullptr);
    DestroyMenu(menu);
}

} // namespace hardcap
