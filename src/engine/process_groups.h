#pragma once

#include "platform/process_monitor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hardcap {

struct ProcessGroup {
    std::wstring key;
    std::wstring display_name;
    std::wstring executable_path;
    std::uint32_t process_count{0};
    std::uint32_t available_count{0};
    double cpu_percent{0};
    std::uint64_t memory_bytes{0};
    size_t largest_memory_process_index{0};
    bool has_target_path{false};
    std::wstring state;
};

std::vector<ProcessGroup> build_process_groups(const std::vector<ProcessInfo>& processes);

} // namespace hardcap
