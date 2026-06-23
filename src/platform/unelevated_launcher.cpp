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

UniqueHandle shell_primary_token() {
    const HWND shell_window = GetShellWindow();
    if (!shell_window) return {};
    DWORD shell_pid = 0;
    GetWindowThreadProcessId(shell_window, &shell_pid);
    UniqueHandle shell_process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid));
    if (!shell_process) return {};
    HANDLE shell_token = nullptr;
    if (!OpenProcessToken(shell_process.get(), TOKEN_DUPLICATE | TOKEN_QUERY, &shell_token)) return {};
    UniqueHandle token(shell_token);
    HANDLE primary = nullptr;
    const BOOL duplicated = DuplicateTokenEx(token.get(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
                                                          TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                            nullptr, SecurityImpersonation, TokenPrimary, &primary);
    return duplicated ? UniqueHandle(primary) : UniqueHandle{};
}

} // namespace

DWORD process_integrity_rid(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return 0;
    UniqueHandle owned_token(token);
    const DWORD result = token_integrity(owned_token.get());
    return result;
}

DWORD current_process_integrity_rid() { return process_integrity_rid(GetCurrentProcess()); }

SuspendedProcess::~SuspendedProcess() = default;

SuspendedProcess::SuspendedProcess(SuspendedProcess&& other) noexcept
    : process(std::move(other.process)), thread(std::move(other.thread)),
      pid(std::exchange(other.pid, 0)), error(std::move(other.error)) {}

SuspendedProcess& SuspendedProcess::operator=(SuspendedProcess&& other) noexcept {
    if (this != &other) {
        process = std::move(other.process);
        thread = std::move(other.thread);
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
        UniqueHandle token = shell_primary_token();
        if (!token) {
            result.error = L"Could not obtain the interactive shell token.";
            return result;
        }
        if (token_integrity(token.get()) > SECURITY_MANDATORY_MEDIUM_RID) {
            result.error = L"Could not obtain a non-elevated interactive shell token.";
            return result;
        }
        void* environment = nullptr;
        CreateEnvironmentBlock(&environment, token.get(), FALSE);
        created = CreateProcessWithTokenW(token.get(), LOGON_WITH_PROFILE, executable.c_str(), command.data(),
                                          CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment,
                                          working_directory.c_str(), &startup, &information);
        if (environment) DestroyEnvironmentBlock(environment);
    }

    if (!created) {
        result.error = GetLastError() == ERROR_ELEVATION_REQUIRED
            ? L"This application requires elevation. Launch it normally, then attach HardCap."
            : error_message(L"Starting the application");
        return result;
    }
    result.process.reset(information.hProcess);
    result.thread.reset(information.hThread);
    result.pid = information.dwProcessId;
    return result;
}

} // namespace hardcap
