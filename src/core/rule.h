#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hardcap {

enum class RuleState {
    inactive,
    applying,
    enforced,
    partially_enforced,
    paused,
    blocked,
    error,
};

struct Rule {
    std::wstring id;
    std::wstring executable_path;
    std::wstring display_name;
    bool enabled{true};
    bool cpu_enabled{false};
    std::uint32_t cpu_percent{25};
    bool memory_enabled{false};
    std::uint64_t memory_bytes{1024ULL * 1024 * 1024};
};

struct RuleStatus {
    RuleState state{RuleState::inactive};
    std::wstring detail;
    std::uint32_t assigned_processes{0};
};

constexpr std::uint64_t minimum_memory_limit = 16ULL * 1024 * 1024;

std::optional<std::wstring> validate_rule(const Rule& rule, std::uint64_t system_commit_limit);
std::optional<std::uint32_t> parse_cpu_percent_text(std::wstring_view text);
std::optional<std::uint64_t> parse_memory_mib_text(std::wstring_view text);
std::uint32_t cpu_percent_to_rate(std::uint32_t percent);
std::wstring create_rule_id();
std::wstring normalize_executable_path(const std::wstring& path);

} // namespace hardcap
