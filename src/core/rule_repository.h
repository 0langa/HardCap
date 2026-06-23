#pragma once

#include "core/rule.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace hardcap {

struct RuleLoadResult {
    std::vector<Rule> rules;
    std::wstring warning;
};

class RuleRepository {
public:
    explicit RuleRepository(std::filesystem::path path);

    RuleLoadResult load() const;
    std::optional<std::wstring> save(const std::vector<Rule>& rules) const;
    const std::filesystem::path& path() const noexcept { return path_; }

    static std::filesystem::path default_path();

private:
    std::filesystem::path path_;
};

} // namespace hardcap
