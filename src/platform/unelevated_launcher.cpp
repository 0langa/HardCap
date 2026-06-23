#include "platform/unelevated_launcher.h"

#include <userenv.h>

#include <utility>
#include <vector>

namespace hardcap {
namespace {

std::wstring error_message(const wchar_t* action) {
    return std::wstring(action) + L" failed (Windows error " + std::to_wstring(GetLastError()) + L").";
}

DWORD token_integrity(HANDLE token) {
    DWORD needed = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &needed);
    if (!needed) return 0;
    std::vector<std::byte> storage(needed);
    if (!GetTokenInformation(token, TokenIntegrityLevel, storage.data(), needed, &needed)) return 0;
    const auto label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(storage.data());
    const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid);
    return count ? *GetSidSubAuthority(label->Label.Sid, count - 1) : 0;
}

HANDLE shell_primary_token() {
    const HWND shell_window = GetShellWindow();
    if (!shell_window) return nullptr;
    DWORD shell_pid = 0;
    GetWindowThreadProcessId(shell_window, &shell_pid);
    const HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
    if (!shell_process) return nullptr;
    HANDLE shell_token = nullptr;
    if (!OpenProcessToken(shell_process, TOKEN_DUPLICATE | TOKEN_QUERY, &shell_token)) {
        CloseHandle(shell_process);
        return nullptr;
    }
    HANDLE primary = nullptr;
    const BOOL duplicated = DuplicateTokenEx(shell_token, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
                                                              TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                              nullptr, SecurityImpersonation, TokenPrimary, &primary);
    CloseHandle(shell_token);
    CloseHandle(shell_process);
    return duplicated ? primary : nullptr;
}

} // namespace

DWORD process_integrity_rid(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return 0;
    const DWORD result = token_integrity(token);
    CloseHandle(token);
    return result;
}

DWORD current_process_integrity_rid() { return process_integrity_rid(GetCurrentProcess()); }

SuspendedProcess::~SuspendedProcess() {
    if (thread) CloseHandle(thread);
    if (process) CloseHandle(process);
}

SuspendedProcess::SuspendedProcess(SuspendedProcess&& other) noexcept
    : process(std::exchange(other.process, nullptr)), thread(std::exchange(other.thread, nullptr)),
      pid(std::exchange(other.pid, 0)), error(std::move(other.error)) {}

SuspendedProcess& SuspendedProcess::operator=(SuspendedProcess&& other) noexcept {
    if (this != &other) {
        if (thread) CloseHandle(thread);
        if (process) CloseHandle(process);
        process = std::exchange(other.process, nullptr);
        thread = std::exchange(other.thread, nullptr);
        pid = std::exchange(other.pid, 0);
        error = std::move(other.error);
    }
    return *this;
}

SuspendedProcess UnelevatedLauncher::launch_suspended(const std::filesystem::path& executable) {
    SuspendedProcess result;
    if (!std::filesystem::is_regular_file(executable)) {
        result.error = L"The executable does not exist.";
        return result;
    }

    std::wstring command = L"\"" + executable.wstring() + L"\"";
    const std::wstring working_directory = executable.parent_path().wstring();
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION information{};
    BOOL created = FALSE;

    if (current_process_integrity_rid() <= SECURITY_MANDATORY_MEDIUM_RID) {
        created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                 CREATE_SUSPENDED, nullptr, working_directory.c_str(), &startup, &information);
    } else {
        const HANDLE token = shell_primary_token();
        if (!token) {
            result.error = L"Could not obtain the interactive shell token.";
            return result;
        }
        void* environment = nullptr;
        CreateEnvironmentBlock(&environment, token, FALSE);
        created = CreateProcessWithTokenW(token, LOGON_WITH_PROFILE, executable.c_str(), command.data(),
                                          CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment,
                                          working_directory.c_str(), &startup, &information);
        if (environment) DestroyEnvironmentBlock(environment);
        CloseHandle(token);
    }

    if (!created) {
        result.error = GetLastError() == ERROR_ELEVATION_REQUIRED
            ? L"This application requires elevation. Launch it normally, then attach HardCap."
            : error_message(L"Starting the application");
        return result;
    }
    result.process = information.hProcess;
    result.thread = information.hThread;
    result.pid = information.dwProcessId;
    return result;
}

} // namespace hardcap
