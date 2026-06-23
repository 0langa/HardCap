#pragma once

#include "core/rule_repository.h"
#include "engine/rule_engine.h"
#include "platform/process_monitor.h"
#include "ui/tray_controller.h"

#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <optional>
#include <vector>

namespace hardcap {

class MainWindow {
public:
    MainWindow(HINSTANCE instance, RuleRepository repository);
    ~MainWindow();
    int run(int show_command);

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    bool create_window(int show_command);
    void create_controls();
    void layout_controls(int width, int height);
    void refresh(bool rescan);
    void populate_list();
    void update_list_columns();
    void handle_column_click(int column);
    void select_row(int row);
    void load_editor(const Rule* rule, const ProcessInfo* process);
    void save_editor();
    void remove_selected_rule();
    void toggle_selected_rule();
    void launch_selected_rule();
    void browse_executable();
    void toggle_pause();
    void show_main_window();
    void exit_application();
    void set_status(const std::wstring& text, bool error = false);
    void paint();
    void release_device_resources();
    std::uint64_t system_commit_limit() const;

    HINSTANCE instance_{};
    HWND window_{};
    HFONT font_{};
    HBRUSH background_brush_{};
    RuleRepository repository_;
    RuleEngine engine_;
    ProcessMonitor monitor_;
    TrayController tray_;
    std::vector<ProcessInfo> processes_;
    std::vector<size_t> visible_rows_;
    int sort_column_{0};
    bool sort_ascending_{true};
    bool updating_list_{false};
    bool rules_mode_{false};
    bool exiting_{false};
    bool light_theme_{true};
    std::wstring selected_rule_id_;
    std::wstring selected_path_;
    std::wstring selected_name_;
    DWORD selected_process_pid_{0};
    std::uint64_t selected_process_creation_time_{0};
    std::wstring load_warning_;

    HWND search_{};
    HWND running_button_{};
    HWND rules_button_{};
    HWND browse_button_{};
    HWND all_processes_{};
    HWND pause_button_{};
    HWND list_{};
    HWND selection_title_{};
    HWND selection_path_{};
    HWND cpu_enabled_{};
    HWND cpu_value_{};
    HWND memory_enabled_{};
    HWND memory_value_{};
    HWND save_button_{};
    HWND launch_button_{};
    HWND disable_button_{};
    HWND remove_button_{};
    HWND warning_{};
    HWND status_{};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
};

} // namespace hardcap
