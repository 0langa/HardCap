#pragma once

#include "core/rule.h"
#include "platform/job_controller.h"
#include "platform/process_monitor.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace hardcap {

struct LaunchResult {
    DWORD pid{0};
    std::wstring error;
};

std::map<std::wstring, std::vector<DWORD>> build_rule_assignments(
    const std::vector<Rule>& rules, const std::vector<ProcessInfo>& processes);

class RuleEngine {
public:
    void set_rules(std::vector<Rule> rules);
    const std::vector<Rule>& rules() const noexcept { return rules_; }
    const std::map<std::wstring, RuleStatus>& statuses() const noexcept { return statuses_; }

    void reconcile(const std::vector<ProcessInfo>& processes);
    LaunchResult launch_limited(const std::wstring& rule_id);
    void set_paused(bool paused);
    bool paused() const noexcept { return paused_; }
    void shutdown();

private:
    std::vector<Rule> rules_;
    std::map<std::wstring, RuleStatus> statuses_;
    std::map<std::wstring, std::unique_ptr<JobController>> jobs_;
    bool paused_{false};
};

} // namespace hardcap
