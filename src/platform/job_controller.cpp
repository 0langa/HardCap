#include "platform/job_controller.h"

#include <algorithm>

namespace hardcap {
namespace {

std::wstring windows_error(const wchar_t* action) {
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result(action);
    result += L" failed (" + std::to_wstring(code) + L")";
    if (text) {
        result += L": ";
        result += text;
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
        LocalFree(text);
    }
    return result;
}

std::wstring safe_job_name(std::wstring id) {
    for (auto& c : id) {
        if (!(c >= L'0' && c <= L'9') && !(c >= L'a' && c <= L'z') &&
            !(c >= L'A' && c <= L'Z') && c != L'-') c = L'-';
    }
    return L"Local\\HardCap." + id;
}

} // namespace

JobController::JobController(const std::wstring& rule_id) : name_(safe_job_name(rule_id)) {
    job_ = CreateJobObjectW(nullptr, name_.c_str());
    if (job_) {
        completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
        association.CompletionKey = job_;
        association.CompletionPort = completion_port_;
        if (!completion_port_ || !SetInformationJobObject(job_, JobObjectAssociateCompletionPortInformation,
                                                           &association, sizeof(association))) {
            if (completion_port_) CloseHandle(completion_port_);
            completion_port_ = nullptr;
        }
    }
}

JobController::~JobController() {
    if (completion_port_) CloseHandle(completion_port_);
    if (job_) CloseHandle(job_);
}

JobController::JobController(JobController&& other) noexcept
    : job_(std::exchange(other.job_, nullptr)),
      completion_port_(std::exchange(other.completion_port_, nullptr)), name_(std::move(other.name_)) {}

JobController& JobController::operator=(JobController&& other) noexcept {
    if (this != &other) {
        if (job_) CloseHandle(job_);
        if (completion_port_) CloseHandle(completion_port_);
        job_ = std::exchange(other.job_, nullptr);
        completion_port_ = std::exchange(other.completion_port_, nullptr);
        name_ = std::move(other.name_);
    }
    return *this;
}

std::optional<std::wstring> JobController::apply_limits(const Rule& rule) {
    if (!job_) return L"Could not create the Job Object.";

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    if (rule.cpu_enabled) {
        cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpu.CpuRate = cpu_percent_to_rate(rule.cpu_percent);
        if (!SetInformationJobObject(job_, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu))) {
            return windows_error(L"Setting the CPU limit");
        }
    } else {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION current{};
        if (QueryInformationJobObject(job_, JobObjectCpuRateControlInformation, &current, sizeof(current), nullptr) &&
            (current.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) != 0 &&
            !SetInformationJobObject(job_, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu))) {
            return windows_error(L"Removing the CPU limit");
        }
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (rule.memory_enabled) {
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_JOB_MEMORY;
        limits.JobMemoryLimit = static_cast<SIZE_T>(rule.memory_bytes);
    }
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const auto memory_error = windows_error(L"Setting the memory limit");
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION reset{};
        SetInformationJobObject(job_, JobObjectCpuRateControlInformation, &reset, sizeof(reset));
        return memory_error;
    }
    return std::nullopt;
}

std::optional<std::wstring> JobController::lift_limits() {
    if (!job_) return L"The Job Object is unavailable.";
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION current{};
    if (QueryInformationJobObject(job_, JobObjectCpuRateControlInformation, &current, sizeof(current), nullptr) &&
        (current.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) != 0 &&
        !SetInformationJobObject(job_, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu))) {
        return windows_error(L"Removing the CPU limit");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return windows_error(L"Removing the memory limit");
    }
    return std::nullopt;
}

std::optional<std::wstring> JobController::assign_process(HANDLE process) const {
    if (!job_ || !process) return L"The process or Job Object handle is invalid.";
    if (!AssignProcessToJobObject(job_, process)) return windows_error(L"Assigning the process");
    return std::nullopt;
}

std::uint32_t JobController::active_process_count() const {
    if (!job_) return 0;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information{};
    if (!QueryInformationJobObject(job_, JobObjectBasicAccountingInformation,
                                   &information, sizeof(information), nullptr)) return 0;
    return information.ActiveProcesses;
}

std::optional<JobEvent> JobController::wait_for_event(const DWORD timeout_ms) const {
    if (!completion_port_) return std::nullopt;
    DWORD message = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED value = nullptr;
    if (!GetQueuedCompletionStatus(completion_port_, &message, &key, &value, timeout_ms)) return std::nullopt;
    return JobEvent{message, reinterpret_cast<ULONG_PTR>(value)};
}

} // namespace hardcap
