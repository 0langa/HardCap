#pragma once

#include "core/rule.h"

#include <windows.h>

#include <optional>
#include <string>

namespace hardcap {

struct JobEvent {
    DWORD message{0};
    ULONG_PTR value{0};
};

class JobController {
public:
    explicit JobController(const std::wstring& rule_id);
    ~JobController();

    JobController(const JobController&) = delete;
    JobController& operator=(const JobController&) = delete;
    JobController(JobController&& other) noexcept;
    JobController& operator=(JobController&& other) noexcept;

    bool valid() const noexcept { return job_ != nullptr; }
    HANDLE native_handle() const noexcept { return job_; }
    const std::wstring& name() const noexcept { return name_; }

    std::optional<std::wstring> apply_limits(const Rule& rule);
    std::optional<std::wstring> lift_limits();
    std::optional<std::wstring> assign_process(HANDLE process) const;
    std::uint32_t active_process_count() const;
    std::optional<JobEvent> wait_for_event(DWORD timeout_ms = 0) const;

private:
    HANDLE job_{nullptr};
    HANDLE completion_port_{nullptr};
    std::wstring name_;
};

} // namespace hardcap
