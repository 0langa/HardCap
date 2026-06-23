#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace hardcap {

DWORD process_integrity_rid(HANDLE process);
DWORD current_process_integrity_rid();

struct SuspendedProcess {
    HANDLE process{nullptr};
    HANDLE thread{nullptr};
    DWORD pid{0};
    std::wstring error;

    SuspendedProcess() = default;
    ~SuspendedProcess();
    SuspendedProcess(const SuspendedProcess&) = delete;
    SuspendedProcess& operator=(const SuspendedProcess&) = delete;
    SuspendedProcess(SuspendedProcess&& other) noexcept;
    SuspendedProcess& operator=(SuspendedProcess&& other) noexcept;
};

class UnelevatedLauncher {
public:
    static SuspendedProcess launch_suspended(const std::filesystem::path& executable);
};

} // namespace hardcap
