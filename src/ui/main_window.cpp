#include "ui/main_window.h"

#include "core/rule.h"
#include "resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>

namespace hardcap {
namespace {

constexpr wchar_t window_class[] = L"HardCap.MainWindow";
constexpr UINT_PTR refresh_timer = 1;
constexpr UINT process_started_message = WM_APP + 43;
constexpr int header_height = 76;
constexpr int margin = 18;

enum ControlId : int {
    id_search = 1001, id_running, id_rules, id_browse, id_all, id_pause, id_list,
    id_cpu_enabled, id_cpu_value, id_memory_enabled, id_memory_value,
    id_save, id_launch, id_disable, id_remove,
};

bool system_uses_light_theme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

std::wstring get_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void set_list_column(HWND list, int index, const std::wstring& title, int width) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.pszText = const_cast<wchar_t*>(title.c_str());
    column.cx = width;
    column.fmt = index == 0 ? LVCFMT_LEFT : LVCFMT_RIGHT;
    if (ListView_GetColumnWidth(list, index) == 0) ListView_InsertColumn(list, index, &column);
    else ListView_SetColumn(list, index, &column);
}

std::wstring sorted_title(const wchar_t* title, const int column, const int sorted_column, const bool ascending) {
    std::wstring result = title;
    if (column == sorted_column) result += ascending ? L" ▲" : L" ▼";
    return result;
}

int compare_text(const std::wstring& left, const std::wstring& right) {
    const int result = _wcsicmp(left.c_str(), right.c_str());
    if (result < 0) return -1;
    if (result > 0) return 1;
    return 0;
}

COLORREF surface_color(const bool light_theme) {
    return light_theme ? RGB(255, 255, 255) : RGB(30, 30, 30);
}

COLORREF text_color(const bool light_theme) {
    return light_theme ? RGB(20, 20, 20) : RGB(245, 245, 245);
}

template <typename T>
int compare_value(const T& left, const T& right) {
    if (left < right) return -1;
    if (right < left) return 1;
    return 0;
}

const wchar_t* state_text(const RuleState state) {
    switch (state) {
    case RuleState::applying: return L"Applying";
    case RuleState::enforced: return L"Enforced";
    case RuleState::partially_enforced: return L"Partial";
    case RuleState::paused: return L"Paused";
    case RuleState::blocked: return L"Blocked";
    case RuleState::error: return L"Error";
    default: return L"Waiting";
    }
}

} // namespace

MainWindow::MainWindow(HINSTANCE instance, RuleRepository repository)
    : instance_(instance), repository_(std::move(repository)) {
    const auto loaded = repository_.load();
    load_warning_ = loaded.warning;
    engine_.set_rules(loaded.rules);
    light_theme_ = system_uses_light_theme();
}

MainWindow::~MainWindow() {
    if (font_) DeleteObject(font_);
    if (background_brush_) DeleteObject(background_brush_);
}

int MainWindow::run(const int show_command) {
    if (!create_window(show_command)) return 1;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

bool MainWindow::create_window(const int show_command) {
    WNDCLASSEXW type{sizeof(type)};
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = window_proc;
    type.hInstance = instance_;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    type.hIconSm = type.hIcon;
    type.lpszClassName = window_class;
    RegisterClassExW(&type);

    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, window_class, L"HardCap", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720, nullptr, nullptr, instance_, this);
    if (!window_) return false;
    BOOL dark = light_theme_ ? FALSE : TRUE;
    DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    const DWORD corner = 2;
    DwmSetWindowAttribute(window_, 33, &corner, sizeof(corner));
    tray_.create(window_, type.hIcon);
    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    SetTimer(window_, refresh_timer, 1000, nullptr);
    refresh(true);
    if (!load_warning_.empty()) set_status(load_warning_, true);
    return true;
}

LRESULT CALLBACK MainWindow::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    return self ? self->handle_message(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT MainWindow::handle_message(const UINT message, const WPARAM wparam, const LPARAM lparam) {
    switch (message) {
    case WM_CREATE: create_controls(); return 0;
    case process_started_message: refresh(true); return 0;
    case WM_SIZE: layout_controls(LOWORD(lparam), HIWORD(lparam)); release_device_resources(); InvalidateRect(window_, nullptr, FALSE); return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {820, 600};
        return 0;
    }
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wparam), light_theme_ ? RGB(30, 30, 30) : RGB(238, 238, 238));
        return reinterpret_cast<LRESULT>(background_brush_);
    case WM_TIMER:
        if (wparam == refresh_timer) refresh(true);
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id == id_search && notification == EN_CHANGE) populate_list();
        else if (id == id_running) { rules_mode_ = false; update_list_columns(); populate_list(); }
        else if (id == id_rules) { rules_mode_ = true; update_list_columns(); populate_list(); }
        else if (id == id_browse) browse_executable();
        else if (id == id_all) refresh(true);
        else if (id == id_pause || id == TrayController::command_pause) toggle_pause();
        else if (id == id_save) save_editor();
        else if (id == id_launch) launch_selected_rule();
        else if (id == id_disable) toggle_selected_rule();
        else if (id == id_remove) remove_selected_rule();
        else if (id == TrayController::command_show) show_main_window();
        else if (id == TrayController::command_exit) exit_application();
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header->idFrom == id_list && header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
            if (!updating_list_ && (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) select_row(change->iItem);
        } else if (header->idFrom == id_list && header->code == LVN_COLUMNCLICK) {
            const auto* click = reinterpret_cast<NMLISTVIEW*>(lparam);
            handle_column_click(click->iSubItem);
        }
        return 0;
    }
    case TrayController::callback_message:
        if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == NIN_SELECT) show_main_window();
        else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) tray_.show_menu(engine_.paused());
        return 0;
    case WM_CLOSE:
        if (exiting_) DestroyWindow(window_); else ShowWindow(window_, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        engine_.shutdown();
        return TRUE;
    case WM_DESTROY:
        KillTimer(window_, refresh_timer);
        monitor_.stop_watching();
        tray_.remove();
        engine_.shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void MainWindow::create_controls() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory_.ReleaseAndGetAddressOf());
    font_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    background_brush_ = CreateSolidBrush(light_theme_ ? RGB(243, 243, 243) : RGB(32, 32, 32));
    auto make = [this](const wchar_t* type, const wchar_t* text, DWORD style, int id) {
        HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                       0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return control;
    };
    search_ = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, id_search);
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search processes and rules"));
    running_button_ = make(L"BUTTON", L"Running", BS_PUSHBUTTON, id_running);
    rules_button_ = make(L"BUTTON", L"Rules", BS_PUSHBUTTON, id_rules);
    browse_button_ = make(L"BUTTON", L"Add executable…", BS_PUSHBUTTON, id_browse);
    all_processes_ = make(L"BUTTON", L"All processes", BS_AUTOCHECKBOX, id_all);
    pause_button_ = make(L"BUTTON", L"Pause all", BS_PUSHBUTTON, id_pause);

    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                            0, 0, 10, 10, window_, reinterpret_cast<HMENU>(id_list), instance_, nullptr);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(list_, light_theme_ ? L"Explorer" : L"DarkMode_Explorer", nullptr);
    ListView_SetBkColor(list_, surface_color(light_theme_));
    ListView_SetTextBkColor(list_, surface_color(light_theme_));
    ListView_SetTextColor(list_, text_color(light_theme_));
    set_list_column(list_, 0, L"Application", 210);
    set_list_column(list_, 1, L"PID", 75);
    set_list_column(list_, 2, L"CPU", 75);
    set_list_column(list_, 3, L"Memory", 100);
    set_list_column(list_, 4, L"State", 110);
    update_list_columns();

    selection_title_ = make(L"STATIC", L"Select an application", SS_LEFT, 0);
    selection_path_ = make(L"STATIC", L"Choose a process or saved rule to configure it.", SS_LEFT | SS_PATHELLIPSIS, 0);
    cpu_enabled_ = make(L"BUTTON", L"CPU hard cap", BS_AUTOCHECKBOX, id_cpu_enabled);
    cpu_value_ = make(L"EDIT", L"25", WS_BORDER | ES_NUMBER | ES_RIGHT, id_cpu_value);
    memory_enabled_ = make(L"BUTTON", L"Memory hard cap", BS_AUTOCHECKBOX, id_memory_enabled);
    memory_value_ = make(L"EDIT", L"1024", WS_BORDER | ES_NUMBER | ES_RIGHT, id_memory_value);
    save_button_ = make(L"BUTTON", L"Save && apply", BS_DEFPUSHBUTTON, id_save);
    launch_button_ = make(L"BUTTON", L"Launch limited", BS_PUSHBUTTON, id_launch);
    disable_button_ = make(L"BUTTON", L"Disable", BS_PUSHBUTTON, id_disable);
    remove_button_ = make(L"BUTTON", L"Remove", BS_PUSHBUTTON, id_remove);
    warning_ = make(L"STATIC", L"Memory is a committed-memory ceiling. Reaching it can make the target fail allocations or exit.", SS_LEFT, 0);
    status_ = make(L"STATIC", L"Administrator · Ready", SS_LEFT, 0);
    EnableWindow(save_button_, FALSE);
    EnableWindow(launch_button_, FALSE);
    EnableWindow(disable_button_, FALSE);
    EnableWindow(remove_button_, FALSE);
    monitor_.start_watching([this] { if (window_) PostMessageW(window_, process_started_message, 0, 0); });
}

void MainWindow::layout_controls(const int width, const int height) {
    if (!search_) return;
    const int content_top = header_height;
    const bool wide = width >= 900;
    const int left_width = wide ? static_cast<int>(width * 0.57) : width - margin * 2;
    const int left_x = margin;
    const int left_y = content_top + 46;
    const int left_height = wide ? height - left_y - 44 : static_cast<int>((height - left_y) * 0.47);
    const int right_x = wide ? left_x + left_width + 26 : margin;
    const int right_y = wide ? content_top + 8 : left_y + left_height + 18;
    const int right_width = wide ? width - right_x - margin : width - margin * 2;

    MoveWindow(running_button_, margin, content_top + 8, 82, 30, TRUE);
    MoveWindow(rules_button_, margin + 88, content_top + 8, 72, 30, TRUE);
    MoveWindow(browse_button_, margin + 166, content_top + 8, 132, 30, TRUE);
    MoveWindow(all_processes_, margin + 306, content_top + 10, 116, 26, TRUE);
    MoveWindow(search_, width - 390, 22, 250, 32, TRUE);
    MoveWindow(pause_button_, width - 128, 22, 110, 32, TRUE);
    MoveWindow(list_, left_x, left_y, left_width, left_height, TRUE);

    int y = right_y + 14;
    MoveWindow(selection_title_, right_x + 14, y, right_width - 28, 28, TRUE); y += 30;
    MoveWindow(selection_path_, right_x + 14, y, right_width - 28, 38, TRUE); y += 52;
    MoveWindow(cpu_enabled_, right_x + 14, y, 160, 28, TRUE);
    MoveWindow(cpu_value_, right_x + 180, y, 74, 28, TRUE);
    HWND cpu_suffix = GetDlgItem(window_, 9001);
    if (!cpu_suffix) {
        cpu_suffix = CreateWindowExW(0, L"STATIC", L"% total CPU", WS_CHILD | WS_VISIBLE,
                                     0, 0, 10, 10, window_, reinterpret_cast<HMENU>(9001), instance_, nullptr);
        SendMessageW(cpu_suffix, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    MoveWindow(cpu_suffix, right_x + 262, y + 4, 120, 24, TRUE); y += 44;
    MoveWindow(memory_enabled_, right_x + 14, y, 160, 28, TRUE);
    MoveWindow(memory_value_, right_x + 180, y, 74, 28, TRUE);
    HWND memory_suffix = GetDlgItem(window_, 9002);
    if (!memory_suffix) {
        memory_suffix = CreateWindowExW(0, L"STATIC", L"MiB committed", WS_CHILD | WS_VISIBLE,
                                        0, 0, 10, 10, window_, reinterpret_cast<HMENU>(9002), instance_, nullptr);
        SendMessageW(memory_suffix, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    MoveWindow(memory_suffix, right_x + 262, y + 4, 120, 24, TRUE); y += 42;
    MoveWindow(warning_, right_x + 14, y, right_width - 28, 48, TRUE); y += 58;
    MoveWindow(save_button_, right_x + 14, y, 124, 34, TRUE);
    MoveWindow(launch_button_, right_x + 146, y, 120, 34, TRUE); y += 44;
    MoveWindow(disable_button_, right_x + 14, y, 110, 32, TRUE);
    MoveWindow(remove_button_, right_x + 132, y, 100, 32, TRUE);
    MoveWindow(status_, margin, height - 30, width - margin * 2, 24, TRUE);
}

void MainWindow::refresh(const bool rescan) {
    if (rescan) {
        const bool include_system = Button_GetCheck(all_processes_) == BST_CHECKED;
        processes_ = monitor_.snapshot(include_system);
        engine_.reconcile(processes_);
    }
    populate_list();
}

void MainWindow::update_list_columns() {
    if (rules_mode_) {
        set_list_column(list_, 0, sorted_title(L"Application", 0, sort_column_, sort_ascending_), 210);
        set_list_column(list_, 1, sorted_title(L"Target", 1, sort_column_, sort_ascending_), 65);
        set_list_column(list_, 2, sorted_title(L"CPU cap", 2, sort_column_, sort_ascending_), 80);
        set_list_column(list_, 3, sorted_title(L"Memory cap", 3, sort_column_, sort_ascending_), 105);
        set_list_column(list_, 4, sorted_title(L"State", 4, sort_column_, sort_ascending_), 110);
    } else {
        set_list_column(list_, 0, sorted_title(L"Application", 0, sort_column_, sort_ascending_), 210);
        set_list_column(list_, 1, sorted_title(L"PID", 1, sort_column_, sort_ascending_), 75);
        set_list_column(list_, 2, sorted_title(L"CPU", 2, sort_column_, sort_ascending_), 75);
        set_list_column(list_, 3, sorted_title(L"Memory", 3, sort_column_, sort_ascending_), 100);
        set_list_column(list_, 4, sorted_title(L"State", 4, sort_column_, sort_ascending_), 110);
    }
}

void MainWindow::handle_column_click(const int column) {
    if (column < 0 || column > 4) return;
    if (sort_column_ == column) {
        sort_ascending_ = !sort_ascending_;
    } else {
        sort_column_ = column;
        sort_ascending_ = !(column == 2 || column == 3);
    }
    update_list_columns();
    populate_list();
}

void MainWindow::populate_list() {
    if (!list_) return;
    const std::wstring query = lower(get_text(search_));
    const std::wstring selected_key = rules_mode_ ? selected_rule_id_ : selected_path_;
    const DWORD selected_pid = selected_process_pid_;
    const std::uint64_t selected_creation_time = selected_process_creation_time_;
    visible_rows_.clear();

    struct RowData {
        size_t source_index{};
        std::wstring key;
        std::array<std::wstring, 5> cells;
        std::uint32_t pid{};
        std::uint64_t creation_time{};
        double cpu{};
        std::uint64_t memory{};
        std::wstring state;
    };
    std::vector<RowData> rows;

    if (rules_mode_) {
        const auto& rules = engine_.rules();
        for (size_t index = 0; index < rules.size(); ++index) {
            const auto& rule = rules[index];
            if (!query.empty() && lower(rule.display_name + L" " + rule.executable_path).find(query) == std::wstring::npos) continue;
            std::wstring cpu = rule.cpu_enabled ? std::to_wstring(rule.cpu_percent) + L"%" : L"Off";
            std::wstring memory = rule.memory_enabled ? std::format(L"{} MiB", rule.memory_bytes / (1024 * 1024)) : L"Off";
            const auto state = engine_.statuses().find(rule.id);
            std::wstring status = !rule.enabled ? L"Disabled" : state == engine_.statuses().end() ? L"Waiting" : state_text(state->second.state);
            rows.push_back(RowData{
                .source_index = index,
                .key = rule.id,
                .cells = {rule.display_name, rule.executable_path, cpu, memory, status},
                .pid = 0,
                .creation_time = 0,
                .cpu = rule.cpu_enabled ? static_cast<double>(rule.cpu_percent) : -1.0,
                .memory = rule.memory_enabled ? rule.memory_bytes : 0,
                .state = status,
            });
        }
    } else {
        for (size_t index = 0; index < processes_.size(); ++index) {
            const auto& process = processes_[index];
            if (!query.empty() && lower(process.display_name + L" " + process.executable_path).find(query) == std::wstring::npos) continue;
            std::wstring pid = std::to_wstring(process.pid);
            std::wstring cpu = std::format(L"{:.1f}%", process.cpu_percent);
            std::wstring memory = std::format(L"{} MiB", process.memory_bytes / (1024 * 1024));
            std::wstring state = process.controllable ? L"Available" : process.block_reason;
            rows.push_back(RowData{
                .source_index = index,
                .key = process.executable_path,
                .cells = {process.display_name, pid, cpu, memory, state},
                .pid = process.pid,
                .creation_time = process.creation_time,
                .cpu = process.cpu_percent,
                .memory = process.memory_bytes,
                .state = state,
            });
        }
    }

    std::stable_sort(rows.begin(), rows.end(), [this](const RowData& left, const RowData& right) {
        int result = 0;
        switch (sort_column_) {
        case 1:
            result = rules_mode_ ? compare_text(left.cells[1], right.cells[1]) : compare_value(left.pid, right.pid);
            break;
        case 2: result = compare_value(left.cpu, right.cpu); break;
        case 3: result = compare_value(left.memory, right.memory); break;
        case 4: result = compare_text(left.state, right.state); break;
        default: result = compare_text(left.cells[0], right.cells[0]); break;
        }
        if (result == 0) result = compare_text(left.cells[0], right.cells[0]);
        if (result == 0) result = compare_text(left.key, right.key);
        return sort_ascending_ ? result < 0 : result > 0;
    });

    updating_list_ = true;
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    const int desired = static_cast<int>(rows.size());
    while (ListView_GetItemCount(list_) > desired) {
        ListView_DeleteItem(list_, ListView_GetItemCount(list_) - 1);
    }

    int selected_row = -1;
    visible_rows_.reserve(rows.size());
    for (int row = 0; row < desired; ++row) {
        const auto& data = rows[static_cast<size_t>(row)];
        visible_rows_.push_back(data.source_index);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(data.cells[0].c_str());
        item.lParam = static_cast<LPARAM>(data.source_index);
        if (row >= ListView_GetItemCount(list_)) ListView_InsertItem(list_, &item);
        else ListView_SetItem(list_, &item);
        for (int column = 1; column < 5; ++column) {
            ListView_SetItemText(list_, row, column, const_cast<wchar_t*>(data.cells[static_cast<size_t>(column)].c_str()));
        }
        if (rules_mode_) {
            if (!selected_key.empty() && data.key == selected_key) selected_row = row;
        } else if (selected_pid != 0 && data.pid == selected_pid && data.creation_time == selected_creation_time) {
            selected_row = row;
        }
    }

    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
    if (selected_row >= 0) {
        ListView_SetItemState(list_, selected_row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    updating_list_ = false;
    InvalidateRect(list_, nullptr, TRUE);
}

void MainWindow::select_row(const int row) {
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!ListView_GetItem(list_, &item)) return;
    const size_t index = static_cast<size_t>(item.lParam);
    if (rules_mode_) {
        if (index >= engine_.rules().size()) return;
        const Rule& rule = engine_.rules()[index];
        load_editor(&rule, nullptr);
    } else {
        if (index >= processes_.size()) return;
        const ProcessInfo& process = processes_[index];
        const auto rule = std::find_if(engine_.rules().begin(), engine_.rules().end(), [&process](const Rule& candidate) {
            return _wcsicmp(candidate.executable_path.c_str(), process.executable_path.c_str()) == 0;
        });
        load_editor(rule == engine_.rules().end() ? nullptr : &*rule, &process);
    }
}

void MainWindow::load_editor(const Rule* rule, const ProcessInfo* process) {
    selected_rule_id_ = rule ? rule->id : L"";
    selected_path_ = rule ? rule->executable_path : process ? process->executable_path : L"";
    selected_name_ = rule ? rule->display_name : process ? process->display_name : L"";
    selected_process_pid_ = process ? process->pid : 0;
    selected_process_creation_time_ = process ? process->creation_time : 0;
    SetWindowTextW(selection_title_, selected_name_.empty() ? L"Select an application" : selected_name_.c_str());
    SetWindowTextW(selection_path_, selected_path_.c_str());
    Button_SetCheck(cpu_enabled_, rule && rule->cpu_enabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(memory_enabled_, rule && rule->memory_enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(cpu_value_, std::to_wstring(rule ? rule->cpu_percent : 25).c_str());
    SetWindowTextW(memory_value_, std::to_wstring((rule ? rule->memory_bytes : 1024ULL * 1024 * 1024) / (1024 * 1024)).c_str());
    const bool available = !selected_path_.empty() && (!process || process->controllable);
    EnableWindow(cpu_enabled_, available);
    EnableWindow(cpu_value_, available);
    EnableWindow(memory_enabled_, available);
    EnableWindow(memory_value_, available);
    EnableWindow(save_button_, available);
    EnableWindow(launch_button_, available && rule != nullptr);
    EnableWindow(disable_button_, available && rule != nullptr);
    EnableWindow(remove_button_, available && rule != nullptr);
    SetWindowTextW(disable_button_, rule && !rule->enabled ? L"Enable" : L"Disable");
    if (process && !process->controllable) set_status(process->block_reason, true);
    else if (rule) {
        const auto status = engine_.statuses().find(rule->id);
        if (status != engine_.statuses().end()) {
            std::wstring detail = status->second.detail;
            if (status->second.assigned_processes != 0) {
                detail += L" · ";
                detail += std::to_wstring(status->second.assigned_processes);
                detail += status->second.assigned_processes == 1 ? L" process assigned" : L" processes assigned";
            }
            set_status(detail, status->second.state == RuleState::blocked || status->second.state == RuleState::error);
        } else {
            set_status(L"Saved rule selected.");
        }
    }
}

void MainWindow::save_editor() {
    if (selected_path_.empty()) return;
    const bool cpu_enabled = Button_GetCheck(cpu_enabled_) == BST_CHECKED;
    const bool memory_enabled = Button_GetCheck(memory_enabled_) == BST_CHECKED;
    const auto cpu = cpu_enabled ? parse_cpu_percent_text(get_text(cpu_value_)) : std::optional<std::uint32_t>{25};
    if (!cpu) { set_status(L"CPU must be a whole percent from 1 to 100.", true); return; }
    const auto memory_bytes = memory_enabled ? parse_memory_mib_text(get_text(memory_value_)) :
        std::optional<std::uint64_t>{1024ULL * 1024ULL * 1024ULL};
    if (!memory_bytes) { set_status(L"Memory must be whole MiB within supported bounds.", true); return; }
    Rule edited{};
    edited.id = selected_rule_id_.empty() ? create_rule_id() : selected_rule_id_;
    edited.executable_path = normalize_executable_path(selected_path_);
    edited.display_name = selected_name_;
    edited.enabled = true;
    edited.cpu_enabled = cpu_enabled;
    edited.cpu_percent = *cpu;
    edited.memory_enabled = memory_enabled;
    edited.memory_bytes = *memory_bytes;
    if (const auto error = validate_rule(edited, system_commit_limit())) { set_status(*error, true); return; }

    auto rules = engine_.rules();
    const auto existing = std::find_if(rules.begin(), rules.end(), [&edited](const Rule& rule) {
        return rule.id == edited.id || _wcsicmp(rule.executable_path.c_str(), edited.executable_path.c_str()) == 0;
    });
    if (existing == rules.end()) rules.push_back(edited); else *existing = edited;
    if (const auto error = repository_.save(rules)) { set_status(*error, true); return; }
    selected_rule_id_ = edited.id;
    engine_.set_rules(std::move(rules));
    engine_.reconcile(processes_);
    set_status(L"Rule saved and applied.");
    load_editor(&*std::find_if(engine_.rules().begin(), engine_.rules().end(), [this](const Rule& rule) { return rule.id == selected_rule_id_; }), nullptr);
    populate_list();
}

void MainWindow::remove_selected_rule() {
    if (selected_rule_id_.empty()) return;
    if (MessageBoxW(window_, L"Remove this saved rule and lift its active limits?", L"Remove rule",
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    auto rules = engine_.rules();
    std::erase_if(rules, [this](const Rule& rule) { return rule.id == selected_rule_id_; });
    if (const auto error = repository_.save(rules)) { set_status(*error, true); return; }
    engine_.set_rules(std::move(rules));
    selected_rule_id_.clear();
    load_editor(nullptr, nullptr);
    set_status(L"Rule removed and limits lifted.");
    populate_list();
}

void MainWindow::toggle_selected_rule() {
    if (selected_rule_id_.empty()) return;
    auto rules = engine_.rules();
    const auto rule = std::find_if(rules.begin(), rules.end(), [this](const Rule& candidate) { return candidate.id == selected_rule_id_; });
    if (rule == rules.end()) return;
    rule->enabled = !rule->enabled;
    if (const auto error = repository_.save(rules)) { set_status(*error, true); return; }
    engine_.set_rules(std::move(rules));
    engine_.reconcile(processes_);
    const auto current = std::find_if(engine_.rules().begin(), engine_.rules().end(), [this](const Rule& candidate) { return candidate.id == selected_rule_id_; });
    load_editor(current == engine_.rules().end() ? nullptr : &*current, nullptr);
    set_status(current != engine_.rules().end() && current->enabled ? L"Rule enabled." : L"Rule disabled; active caps were lifted.");
    populate_list();
}

void MainWindow::launch_selected_rule() {
    if (selected_rule_id_.empty()) return;
    const auto result = engine_.launch_limited(selected_rule_id_);
    if (!result.error.empty()) set_status(result.error, true);
    else set_status(L"Application launched at medium integrity with hard limits active.");
}

void MainWindow::browse_executable() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Applications (*.exe)\0*.exe\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return;
    selected_path_ = normalize_executable_path(path);
    selected_name_ = std::filesystem::path(path).filename().wstring();
    const auto rule = std::find_if(engine_.rules().begin(), engine_.rules().end(), [this](const Rule& candidate) {
        return _wcsicmp(candidate.executable_path.c_str(), selected_path_.c_str()) == 0;
    });
    load_editor(rule == engine_.rules().end() ? nullptr : &*rule, nullptr);
}

void MainWindow::toggle_pause() {
    engine_.set_paused(!engine_.paused());
    if (!engine_.paused()) engine_.reconcile(processes_);
    SetWindowTextW(pause_button_, engine_.paused() ? L"Resume all" : L"Pause all");
    set_status(engine_.paused() ? L"All limits are paused." : L"All enabled limits resumed.");
    populate_list();
}

void MainWindow::show_main_window() {
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
}

void MainWindow::exit_application() {
    exiting_ = true;
    engine_.shutdown();
    DestroyWindow(window_);
}

void MainWindow::set_status(const std::wstring& text, const bool error) {
    const std::wstring message = (error ? L"⚠ " : L"✓ ") + text;
    SetWindowTextW(status_, message.c_str());
}

void MainWindow::paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    if (!render_target_ && d2d_factory_) {
        const auto size = D2D1::SizeU(client.right, client.bottom);
        d2d_factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                             D2D1::HwndRenderTargetProperties(window_, size),
                                             render_target_.ReleaseAndGetAddressOf());
        if (render_target_) {
            render_target_->CreateSolidColorBrush(light_theme_ ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.84f) : D2D1::ColorF(0.16f, 0.16f, 0.16f), surface_brush_.ReleaseAndGetAddressOf());
            render_target_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.84f), accent_brush_.ReleaseAndGetAddressOf());
        }
    }
    if (render_target_) {
        render_target_->BeginDraw();
        render_target_->Clear(light_theme_ ? D2D1::ColorF(0.953f, 0.953f, 0.953f) : D2D1::ColorF(0.125f, 0.125f, 0.125f));
        render_target_->FillRectangle(D2D1::RectF(0, 0, static_cast<float>(client.right), 5), accent_brush_.Get());
        const bool wide = client.right >= 900;
        const float right_x = wide ? static_cast<float>(margin + static_cast<int>(client.right * 0.57) + 26) : static_cast<float>(margin);
        const float right_y = wide ? static_cast<float>(header_height + 8) : static_cast<float>(client.bottom * 0.54);
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(right_x, right_y, static_cast<float>(client.right - margin),
                                          static_cast<float>(client.bottom - 38)), 12.0f, 12.0f), surface_brush_.Get());
        render_target_->EndDraw();
    }
    EndPaint(window_, &paint);
}

void MainWindow::release_device_resources() {
    surface_brush_.Reset();
    accent_brush_.Reset();
    render_target_.Reset();
}

std::uint64_t MainWindow::system_commit_limit() const {
    PERFORMANCE_INFORMATION info{sizeof(info)};
    return GetPerformanceInfo(&info, sizeof(info))
        ? static_cast<std::uint64_t>(info.CommitLimit) * info.PageSize : 0;
}

} // namespace hardcap
