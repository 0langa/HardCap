#include "engine/rule_engine.h"

#include "core/rule.h"
#include "platform/unelevated_launcher.h"
#include "platform/win32_handle.h"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace hardcap {
namespace {

const Rule* matching_rule(const std::wstring& path, const std::vector<Rule>& rules) {
    for (const auto& rule : rules) {
        if (rule.enabled && _wcsicmp(rule.executable_path.c_str(), path.c_str()) == 0) return &rule;
    }
    return nullptr;
}

} // namespace

std::map<std::wstring, std::vector<DWORD>> build_rule_assignments(
    const std::vector<Rule>& rules, const std::vector<ProcessInfo>& processes) {
    std::unordered_map<DWORD, const ProcessInfo*> by_pid;
    for (const auto& process : processes) by_pid[process.pid] = &process;

    std::map<std::wstring, std::vector<DWORD>> result;
    for (const auto& process : processes) {
        std::vector<const ProcessInfo*> lineage;
        const ProcessInfo* cursor = &process;
        std::set<DWORD> visited;
        while (cursor && visited.insert(cursor->pid).second) {
            lineage.push_back(cursor);
            const auto parent = by_pid.find(cursor->parent_pid);
            cursor = parent == by_pid.end() ? nullptr : parent->second;
        }
        std::reverse(lineage.begin(), lineage.end());
        for (const auto* ancestor : lineage) {
            if (const Rule* owner = matching_rule(ancestor->executable_path, rules)) {
                result[owner->id].push_back(process.pid);
                break;
            }
        }
    }
    return result;
}

void RuleEngine::set_rules(std::vector<Rule> rules) {
    rules_ = std::move(rules);
    for (auto& rule : rules_) {
        rule.executable_path = normalize_executable_path(rule.executable_path);
        if (rule.id.empty()) rule.id = create_rule_id();
    }
    std::set<std::wstring> current;
    for (const auto& rule : rules_) current.insert(rule.id);
    std::erase_if(statuses_, [&current](const auto& pair) { return !current.contains(pair.first); });
    for (auto iterator = jobs_.begin(); iterator != jobs_.end();) {
        if (!current.contains(iterator->first)) {
            iterator->second->lift_limits();
            iterator = jobs_.erase(iterator);
        } else ++iterator;
    }
}

void RuleEngine::reconcile(const std::vector<ProcessInfo>& processes) {
    const auto assignments = build_rule_assignments(rules_, processes);
    std::unordered_map<DWORD, const ProcessInfo*> by_pid;
    for (const auto& process : processes) by_pid[process.pid] = &process;

    for (const auto& rule : rules_) {
        auto& status = statuses_[rule.id];
        if (!rule.enabled || paused_) {
            if (const auto existing = jobs_.find(rule.id); existing != jobs_.end()) {
                if (const auto error = existing->second->lift_limits()) {
                    status = {RuleState::error, *error, existing->second->active_process_count()};
                    continue;
                }
            }
            status = {paused_ && rule.enabled ? RuleState::paused : RuleState::inactive,
                      paused_ && rule.enabled ? L"Globally paused" : L"Rule disabled", 0};
            continue;
        }
        auto& job = jobs_[rule.id];
        if (!job) job = std::make_unique<JobController>(rule.id);
        if (!job->valid()) {
            status = {RuleState::error, L"Could not create or recover the Job Object", 0};
            continue;
        }
        if (const auto error = job->apply_limits(rule)) {
            status = {RuleState::error, *error, 0};
            continue;
        }

        std::uint32_t assigned = 0;
        std::wstring last_error;
        bool memory_limit_reached = false;
        if (const auto found = assignments.find(rule.id); found != assignments.end()) {
            for (const DWORD pid : found->second) {
                const auto info = by_pid.find(pid);
                if (info == by_pid.end()) continue;
                if (!info->second->controllable) {
                    last_error = L"PID " + std::to_wstring(pid) + L" was skipped: " + info->second->block_reason;
                    continue;
                }
                UniqueHandle process(OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE |
                                                     PROCESS_QUERY_LIMITED_INFORMATION,
                                                 FALSE, pid));
                if (!process) { last_error = L"Access denied while opening PID " + std::to_wstring(pid); continue; }
                BOOL already = FALSE;
                if (IsProcessInJob(process.get(), job->native_handle(), &already) && already) {
                    ++assigned;
                } else if (const auto error = job->assign_process(process.get())) {
                    last_error = *error;
                } else {
                    ++assigned;
                }
            }
        }
        while (const auto event = job->wait_for_event()) {
            if (event->message == JOB_OBJECT_MSG_JOB_MEMORY_LIMIT) memory_limit_reached = true;
        }
        if (!last_error.empty()) status = {assigned ? RuleState::partially_enforced : RuleState::blocked, last_error, assigned};
        else if (assigned) status = {RuleState::enforced,
                                     memory_limit_reached ? L"Memory ceiling reached; an allocation was denied" : L"Hard limits active",
                                     assigned};
        else status = {RuleState::inactive, L"Waiting for the application", 0};
    }
}

LaunchResult RuleEngine::launch_limited(const std::wstring& rule_id) {
    const auto rule = std::find_if(rules_.begin(), rules_.end(), [&rule_id](const Rule& candidate) {
        return candidate.id == rule_id;
    });
    if (rule == rules_.end()) return {0, L"The selected rule no longer exists."};
    if (!rule->enabled) return {0, L"Enable the rule before launching the application."};

    auto& job = jobs_[rule->id];
    if (!job) job = std::make_unique<JobController>(rule->id);
    if (!job->valid()) return {0, L"Could not create the Job Object."};
    if (const auto error = job->apply_limits(*rule)) return {0, *error};

    auto process = UnelevatedLauncher::launch_suspended(rule->executable_path);
    if (!process.error.empty()) return {0, process.error};
    if (const auto error = job->assign_process(process.process.get())) {
        TerminateProcess(process.process.get(), ERROR_ACCESS_DENIED);
        ResumeThread(process.thread.get());
        return {0, *error};
    }
    if (ResumeThread(process.thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.process.get(), error);
        return {0, L"Could not resume the limited application (Windows error " + std::to_wstring(error) + L")."};
    }
    statuses_[rule->id] = {RuleState::enforced, L"Hard limits active", 1};
    return {process.pid, {}};
}

void RuleEngine::set_paused(const bool paused) {
    paused_ = paused;
    if (paused_) {
        for (auto& [id, job] : jobs_) {
            if (const auto error = job->lift_limits()) {
                statuses_[id] = {RuleState::error, *error, job->active_process_count()};
            }
        }
        for (const auto& rule : rules_) {
            if (!rule.enabled) {
                statuses_[rule.id] = {RuleState::inactive, L"Rule disabled", 0};
            } else if (const auto status = statuses_.find(rule.id);
                       status == statuses_.end() || status->second.state != RuleState::error) {
                statuses_[rule.id] = {RuleState::paused, L"Globally paused", 0};
            }
        }
    }
}

void RuleEngine::shutdown() {
    for (auto& [id, job] : jobs_) {
        static_cast<void>(id);
        job->lift_limits();
    }
    jobs_.clear();
}

} // namespace hardcap
