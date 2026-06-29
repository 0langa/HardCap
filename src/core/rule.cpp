#include "core/rule.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cwctype>
#include <limits>

namespace hardcap {
namespace {

std::optional<std::uint64_t> parse_unsigned_text(const std::wstring_view text, const std::uint64_t max_value) {
    if (text.empty()) return std::nullopt;
    std::uint64_t value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(c - L'0');
        if (value > (max_value - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

} // namespace

std::optional<std::wstring> validate_rule(const Rule& rule, const std::uint64_t system_commit_limit) {
    if (!rule.cpu_enabled && !rule.memory_enabled) return L"Enable at least one limit.";
    if (rule.cpu_enabled && (rule.cpu_percent < 1 || rule.cpu_percent > 100)) {
        return L"CPU must be between 1 and 100 percent.";
    }
    if (rule.memory_enabled) {
        if (rule.memory_bytes < minimum_memory_limit) return L"Memory must be at least 16 MiB.";
        if (system_commit_limit != 0 && rule.memory_bytes > system_commit_limit) {
            return L"Memory cannot exceed the system commit limit.";
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> parse_cpu_percent_text(const std::wstring_view text) {
    const auto parsed = parse_unsigned_text(text, 100);
    if (!parsed || *parsed < 1 || *parsed > 100) return std::nullopt;
    return static_cast<std::uint32_t>(*parsed);
}

std::optional<std::uint64_t> parse_memory_mib_text(const std::wstring_view text) {
    constexpr std::uint64_t bytes_per_mib = 1024ULL * 1024ULL;
    const auto parsed = parse_unsigned_text(text, std::numeric_limits<std::uint64_t>::max() / bytes_per_mib);
    if (!parsed) return std::nullopt;
    return *parsed * bytes_per_mib;
}

bool rule_status_needs_attention(const RuleState state) noexcept {
    return state == RuleState::partially_enforced || state == RuleState::blocked || state == RuleState::error;
}

std::wstring upsert_rule_by_executable_path(std::vector<Rule>& rules, Rule edited) {
    edited.executable_path = normalize_executable_path(edited.executable_path);
    if (edited.id.empty()) edited.id = create_rule_id();
    const auto existing = std::find_if(rules.begin(), rules.end(), [&edited](const Rule& rule) {
        return rule.id == edited.id || _wcsicmp(rule.executable_path.c_str(), edited.executable_path.c_str()) == 0;
    });
    if (existing == rules.end()) {
        const std::wstring id = edited.id;
        rules.push_back(std::move(edited));
        return id;
    }
    edited.id = existing->id;
    *existing = std::move(edited);
    return existing->id;
}

std::uint32_t cpu_percent_to_rate(const std::uint32_t percent) {
    return std::clamp(percent, 1U, 100U) * 100U;
}

std::wstring create_rule_id() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t buffer[40]{};
    StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    std::wstring result(buffer);
    result.erase(std::remove(result.begin(), result.end(), L'{'), result.end());
    result.erase(std::remove(result.begin(), result.end(), L'}'), result.end());
    return result;
}

std::wstring normalize_executable_path(const std::wstring& path) {
    if (path.empty()) return {};
    wchar_t full[32768]{};
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    std::wstring normalized = length > 0 && length < std::size(full) ? std::wstring(full, length) : path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    while (normalized.size() > 3 && normalized.back() == L'\\') normalized.pop_back();
    return normalized;
}

} // namespace hardcap
