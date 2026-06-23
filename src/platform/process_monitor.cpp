#include "platform/process_monitor.h"

#include "core/rule.h"
#include "platform/win32_handle.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <wbemidl.h>

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace hardcap {
namespace {

std::uint64_t file_time_value(const FILETIME value) {
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

std::uint64_t now_file_time() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return file_time_value(now);
}

std::uint64_t process_identity(const DWORD pid, const std::uint64_t creation) {
    return (creation * 1099511628211ULL) ^ pid;
}

} // namespace

ProcessMonitor::~ProcessMonitor() { stop_watching(); }

bool ProcessMonitor::start_watching(std::function<void()> on_process_start) {
    if (watcher_thread_.joinable()) return false;
    stop_watcher_ = false;
    watcher_running_ = true;
    try {
        watcher_thread_ = std::thread([this, callback = std::move(on_process_start)] {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            IWbemLocator* locator = nullptr;
            IWbemServices* services = nullptr;
            IEnumWbemClassObject* events = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_IWbemLocator, reinterpret_cast<void**>(&locator)))) {
                BSTR scope = SysAllocString(L"ROOT\\CIMV2");
                const HRESULT connected = locator->ConnectServer(scope, nullptr, nullptr, nullptr, 0,
                                                                  nullptr, nullptr, &services);
                SysFreeString(scope);
                if (SUCCEEDED(connected)) {
                    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
                    BSTR language = SysAllocString(L"WQL");
                    BSTR query = SysAllocString(L"SELECT * FROM Win32_ProcessStartTrace");
                    services->ExecNotificationQuery(language, query,
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &events);
                    SysFreeString(query);
                    SysFreeString(language);
                }
            }
            while (!stop_watcher_ && events) {
                IWbemClassObject* event = nullptr;
                ULONG returned = 0;
                const HRESULT next = events->Next(500, 1, &event, &returned);
                if (SUCCEEDED(next) && returned == 1) {
                    if (callback) callback();
                    event->Release();
                }
            }
            if (events) events->Release();
            if (services) services->Release();
            if (locator) locator->Release();
            if (SUCCEEDED(initialized)) CoUninitialize();
            watcher_running_ = false;
        });
    } catch (...) {
        watcher_running_ = false;
        return false;
    }
    return true;
}

void ProcessMonitor::stop_watching() {
    stop_watcher_ = true;
    if (watcher_thread_.joinable()) watcher_thread_.join();
    watcher_running_ = false;
}

std::vector<ProcessInfo> ProcessMonitor::snapshot(const bool include_system_processes) {
    std::scoped_lock lock(mutex_);
    std::vector<ProcessInfo> result;
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) return result;

    DWORD current_session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &current_session);
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto processors = std::max<DWORD>(1, system.dwNumberOfProcessors);
    const std::uint64_t wall = now_file_time();
    std::unordered_set<std::uint64_t> seen;

    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessInfo item{};
            item.pid = entry.th32ProcessID;
            item.parent_pid = entry.th32ParentProcessID;
            item.display_name = entry.szExeFile;
            ProcessIdToSessionId(item.pid, &item.session_id);
            if (!include_system_processes && item.session_id != current_session) continue;

            UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid));
            if (!process) {
                item.block_reason = L"Access denied or protected process";
                result.push_back(std::move(item));
                continue;
            }

            wchar_t path[32768]{};
            DWORD path_length = static_cast<DWORD>(std::size(path));
            if (QueryFullProcessImageNameW(process.get(), 0, path, &path_length)) {
                item.executable_path = normalize_executable_path(std::wstring(path, path_length));
            }

            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) {
                item.creation_time = file_time_value(created);
                const std::uint64_t used = file_time_value(kernel) + file_time_value(user);
                const auto key = process_identity(item.pid, item.creation_time);
                seen.insert(key);
                if (const auto previous = cpu_samples_.find(key); previous != cpu_samples_.end() && wall > previous->second.wall_time) {
                    const auto process_delta = used >= previous->second.process_time ? used - previous->second.process_time : 0;
                    const auto wall_delta = wall - previous->second.wall_time;
                    item.cpu_percent = std::min(100.0, 100.0 * static_cast<double>(process_delta) /
                        (static_cast<double>(wall_delta) * processors));
                }
                cpu_samples_[key] = {used, wall};
            }

            PROCESS_MEMORY_COUNTERS_EX memory{};
            if (K32GetProcessMemoryInfo(process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
                item.memory_bytes = memory.PrivateUsage;
            }

            BOOL critical = FALSE;
            if (IsProcessCritical(process.get(), &critical)) item.critical = critical != FALSE;
            item.controllable = item.pid != GetCurrentProcessId() && !item.critical && !item.executable_path.empty();
            if (item.pid == GetCurrentProcessId()) item.block_reason = L"HardCap cannot limit itself";
            else if (item.critical) item.block_reason = L"Critical Windows process";
            else if (item.executable_path.empty()) item.block_reason = L"Executable path is unavailable";
            result.push_back(std::move(item));
        } while (Process32NextW(snapshot, &entry));
    }

    std::erase_if(cpu_samples_, [&seen](const auto& pair) { return !seen.contains(pair.first); });
    std::sort(result.begin(), result.end(), [](const ProcessInfo& left, const ProcessInfo& right) {
        const int name = _wcsicmp(left.display_name.c_str(), right.display_name.c_str());
        return name == 0 ? left.pid < right.pid : name < 0;
    });
    return result;
}

} // namespace hardcap
