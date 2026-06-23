#pragma once

#include <windows.h>

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>

namespace hardcap {

struct ProcessInfo {
    DWORD pid{0};
    DWORD parent_pid{0};
    std::uint64_t creation_time{0};
    std::wstring executable_path;
    std::wstring display_name;
    DWORD session_id{0};
    double cpu_percent{0};
    std::uint64_t memory_bytes{0};
    bool controllable{false};
    bool critical{false};
    std::wstring block_reason;
};

class ProcessMonitor {
public:
    ~ProcessMonitor();
    std::vector<ProcessInfo> snapshot(bool include_system_processes);
    bool start_watching(std::function<void()> on_process_start);
    void stop_watching();
    bool watching() const noexcept { return watcher_running_.load(); }

private:
    struct CpuSample { std::uint64_t process_time{0}; std::uint64_t wall_time{0}; };
    std::unordered_map<std::uint64_t, CpuSample> cpu_samples_;
    std::mutex mutex_;
    std::atomic_bool stop_watcher_{false};
    std::atomic_bool watcher_running_{false};
    std::thread watcher_thread_;
};

} // namespace hardcap
