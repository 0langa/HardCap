#include "engine/process_groups.h"

#include <algorithm>
#include <map>

namespace hardcap {
namespace {

std::wstring group_key_for(const ProcessInfo& process) {
    if (!process.executable_path.empty()) return L"path:" + process.executable_path;
    return L"name:" + process.display_name;
}

} // namespace

std::vector<ProcessGroup> build_process_groups(const std::vector<ProcessInfo>& processes) {
    std::map<std::wstring, ProcessGroup> by_key;
    for (size_t index = 0; index < processes.size(); ++index) {
        const auto& process = processes[index];
        const std::wstring key = group_key_for(process);
        auto& group = by_key[key];
        if (group.key.empty()) {
            group.key = key;
            group.display_name = process.display_name;
            group.executable_path = process.executable_path;
            group.largest_memory_process_index = index;
            group.has_target_path = !process.executable_path.empty();
        }
        ++group.process_count;
        if (process.controllable) ++group.available_count;
        group.cpu_percent += process.cpu_percent;
        group.memory_bytes += process.memory_bytes;
        if (process.memory_bytes > processes[group.largest_memory_process_index].memory_bytes) {
            group.largest_memory_process_index = index;
        }
    }

    std::vector<ProcessGroup> groups;
    groups.reserve(by_key.size());
    for (auto& [key, group] : by_key) {
        static_cast<void>(key);
        if (group.available_count == group.process_count) group.state = L"Available";
        else if (group.available_count == 0) group.state = group.has_target_path ? L"Unavailable" : L"Path unavailable";
        else group.state = L"Partial";
        groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end(), [](const ProcessGroup& left, const ProcessGroup& right) {
        const int name = _wcsicmp(left.display_name.c_str(), right.display_name.c_str());
        return name == 0 ? left.key < right.key : name < 0;
    });
    return groups;
}

} // namespace hardcap
