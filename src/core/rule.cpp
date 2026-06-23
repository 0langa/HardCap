#include "core/rule.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cwctype>

namespace hardcap {

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
