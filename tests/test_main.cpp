#include "core/rule.h"
#include "core/rule_repository.h"
#include "engine/process_groups.h"
#include "platform/job_controller.h"
#include "platform/process_monitor.h"
#include "engine/rule_engine.h"
#include "platform/unelevated_launcher.h"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    using namespace hardcap;

    Rule valid{};
    valid.cpu_enabled = true;
    valid.cpu_percent = 25;
    expect(validate_rule(valid, 8ULL * 1024 * 1024 * 1024).has_value() == false,
           "25 percent CPU rule is valid");
    expect(cpu_percent_to_rate(25) == 2500, "CPU rate uses hundredths of a percent");
    expect(parse_cpu_percent_text(L"25").value_or(0) == 25, "CPU percent text parses");
    expect(parse_memory_mib_text(L"768").value_or(0) == 768ULL * 1024 * 1024, "memory MiB text parses to bytes");

    expect(!parse_cpu_percent_text(L"").has_value(), "empty CPU input is rejected");
    expect(!parse_cpu_percent_text(L"0").has_value(), "zero CPU input is rejected");
    expect(!parse_cpu_percent_text(L"101").has_value(), "CPU input above 100 is rejected");
    expect(!parse_cpu_percent_text(L"25x").has_value(), "CPU input with trailing characters is rejected");
    expect(!parse_cpu_percent_text(L" 25").has_value(), "CPU input with whitespace is rejected");
    expect(!parse_cpu_percent_text(L"999999999999999999999999").has_value(), "overflowing CPU input is rejected");

    expect(!parse_memory_mib_text(L"").has_value(), "empty memory input is rejected");
    expect(parse_memory_mib_text(L"0").value_or(1) == 0, "zero memory input parses for validation");
    expect(!parse_memory_mib_text(L"64x").has_value(), "memory input with trailing characters is rejected");
    expect(!parse_memory_mib_text(L" 64").has_value(), "memory input with whitespace is rejected");
    expect(!parse_memory_mib_text(L"18446744073709551616").has_value(), "overflowing memory input is rejected");

    Rule empty{};
    expect(validate_rule(empty, 8ULL * 1024 * 1024 * 1024).has_value(),
           "a rule requires at least one cap");

    Rule tiny_memory{};
    tiny_memory.memory_enabled = true;
    tiny_memory.memory_bytes = 15ULL * 1024 * 1024;
    expect(validate_rule(tiny_memory, 8ULL * 1024 * 1024 * 1024).has_value(),
           "memory cap must be at least 16 MiB");

    expect(normalize_executable_path(L"C:/Apps/Test.EXE/") == L"c:\\apps\\test.exe",
           "executable paths are absolute, slash-normalized, and case-folded");

    const auto temp = std::filesystem::temp_directory_path() / L"hardcap-rule-test.json";
    Rule saved{};
    saved.id = L"rule-1";
    saved.executable_path = L"c:\\apps\\demo.exe";
    saved.display_name = L"Demo \"App\"";
    saved.cpu_enabled = true;
    saved.cpu_percent = 31;
    saved.memory_enabled = true;
    saved.memory_bytes = 768ULL * 1024 * 1024;
    RuleRepository repository(temp);
    expect(repository.save({saved}).has_value() == false, "rules save atomically");
    const auto loaded = repository.load();
    expect(loaded.warning.empty(), "valid settings load without a warning");
    expect(loaded.rules.size() == 1, "one rule round-trips");
    if (loaded.rules.size() == 1) {
        expect(loaded.rules[0].display_name == saved.display_name, "JSON strings round-trip");
        expect(loaded.rules[0].memory_bytes == saved.memory_bytes, "64-bit memory values round-trip");
    }
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);

    wchar_t test_path[32768]{};
    GetModuleFileNameW(nullptr, test_path, static_cast<DWORD>(std::size(test_path)));
    const auto helper_path = std::filesystem::path(test_path).parent_path() / L"hardcap_test_helper.exe";
    std::wstring command = L"\"" + helper_path.wstring() + L"\" spin";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(helper_path.c_str(), command.data(), nullptr, nullptr, FALSE,
                                        CREATE_SUSPENDED, nullptr, helper_path.parent_path().c_str(),
                                        &startup, &process);
    expect(created == TRUE, "test helper starts suspended");
    if (created) {
        Rule job_rule{};
        job_rule.id = L"test-" + create_rule_id();
        job_rule.cpu_enabled = true;
        job_rule.cpu_percent = 1;
        job_rule.memory_enabled = true;
        job_rule.memory_bytes = 128ULL * 1024 * 1024;
        JobController job(job_rule.id);
        expect(job.apply_limits(job_rule).has_value() == false, "Job Object accepts CPU and memory limits");
        expect(job.assign_process(process.hProcess).has_value() == false, "suspended process joins the job");
        const auto joined_event = job.wait_for_event(1000);
        expect(joined_event.has_value(), "Job Object reports membership through its completion port");
        BOOL in_job = FALSE;
        expect(IsProcessInJob(process.hProcess, job.native_handle(), &in_job) && in_job,
               "assigned process reports Job Object membership");
        expect(job.active_process_count() == 1, "job reports one active member");
        FILETIME created_before{}, exited_before{}, kernel_before{}, user_before{}, wall_before{};
        FILETIME created_after{}, exited_after{}, kernel_after{}, user_after{}, wall_after{};
        GetProcessTimes(process.hProcess, &created_before, &exited_before, &kernel_before, &user_before);
        GetSystemTimeAsFileTime(&wall_before);
        ResumeThread(process.hThread);
        Sleep(2500);
        GetProcessTimes(process.hProcess, &created_after, &exited_after, &kernel_after, &user_after);
        GetSystemTimeAsFileTime(&wall_after);
        const auto as_u64 = [](const FILETIME value) {
            ULARGE_INTEGER combined{};
            combined.LowPart = value.dwLowDateTime;
            combined.HighPart = value.dwHighDateTime;
            return combined.QuadPart;
        };
        SYSTEM_INFO cpu_info{};
        GetSystemInfo(&cpu_info);
        const auto used_before = as_u64(kernel_before) + as_u64(user_before);
        const auto used_after = as_u64(kernel_after) + as_u64(user_after);
        const double measured_cpu = 100.0 * static_cast<double>(used_after - used_before) /
            (static_cast<double>(as_u64(wall_after) - as_u64(wall_before)) * cpu_info.dwNumberOfProcessors);
        expect(measured_cpu <= 3.0, "one-percent Job Object CPU cap is enforced within tolerance");
        expect(job.lift_limits().has_value() == false, "limits can be lifted without killing members");
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    std::wstring memory_command = L"\"" + helper_path.wstring() + L"\" allocate 256";
    PROCESS_INFORMATION memory_process{};
    const BOOL memory_created = CreateProcessW(helper_path.c_str(), memory_command.data(), nullptr, nullptr, FALSE,
                                                CREATE_SUSPENDED, nullptr, helper_path.parent_path().c_str(),
                                                &startup, &memory_process);
    expect(memory_created == TRUE, "memory helper starts suspended");
    if (memory_created) {
        Rule memory_rule{};
        memory_rule.id = L"memory-" + create_rule_id();
        memory_rule.memory_enabled = true;
        memory_rule.memory_bytes = 64ULL * 1024 * 1024;
        JobController memory_job(memory_rule.id);
        const auto memory_limit_error = memory_job.apply_limits(memory_rule);
        if (memory_limit_error) std::cerr << "Memory limit diagnostic: " << std::filesystem::path(*memory_limit_error).string() << '\n';
        expect(!memory_limit_error.has_value(), "aggregate memory cap is accepted");
        expect(!memory_job.assign_process(memory_process.hProcess).has_value(), "memory helper joins capped job");
        ResumeThread(memory_process.hThread);
        const DWORD memory_wait = WaitForSingleObject(memory_process.hProcess, 5000);
        expect(memory_wait == WAIT_OBJECT_0, "allocation beyond the job memory cap fails promptly");
        DWORD memory_exit_code = 0;
        GetExitCodeProcess(memory_process.hProcess, &memory_exit_code);
        expect(memory_exit_code == 42, "memory helper observes allocation failure at the cap");
        TerminateProcess(memory_process.hProcess, 0);
        CloseHandle(memory_process.hThread);
        CloseHandle(memory_process.hProcess);
    }

    ProcessMonitor monitor;
    const auto processes = monitor.snapshot(true);
    const DWORD self_pid = GetCurrentProcessId();
    const auto self = std::find_if(processes.begin(), processes.end(),
                                   [self_pid](const ProcessInfo& item) { return item.pid == self_pid; });
    expect(self != processes.end(), "process snapshot contains the caller");
    if (self != processes.end()) {
        expect(!self->executable_path.empty(), "accessible process includes its executable path");
        expect(!self->controllable, "HardCap never allows controlling itself");
    }
    const bool watcher_started = monitor.start_watching([] {});
    monitor.stop_watching();
    expect(watcher_started, "WMI start watcher thread is created");
    expect(!monitor.watching(), "WMI watcher stops and joins cleanly");

    Rule outer{};
    outer.id = L"outer";
    outer.executable_path = L"c:\\apps\\outer.exe";
    outer.cpu_enabled = true;
    Rule inner{};
    inner.id = L"inner";
    inner.executable_path = L"c:\\apps\\inner.exe";
    inner.cpu_enabled = true;
    std::vector<ProcessInfo> tree{
        {.pid = 10, .parent_pid = 0, .executable_path = outer.executable_path, .controllable = true},
        {.pid = 11, .parent_pid = 10, .executable_path = inner.executable_path, .controllable = true},
        {.pid = 12, .parent_pid = 11, .executable_path = L"c:\\apps\\worker.exe", .controllable = true},
    };
    const auto assignments = build_rule_assignments({outer, inner}, tree);
    expect(assignments.at(L"outer").size() == 3, "outer matching rule owns its whole process tree");
    expect(assignments.find(L"inner") == assignments.end() || assignments.at(L"inner").empty(),
           "descendant rule yields to the outermost matching rule");

    std::vector<ProcessInfo> group_processes{
        {.pid = 20, .executable_path = L"c:\\apps\\alpha.exe", .display_name = L"alpha.exe",
         .cpu_percent = 1.5, .memory_bytes = 100, .controllable = true},
        {.pid = 21, .executable_path = L"c:\\apps\\alpha.exe", .display_name = L"alpha helper",
         .cpu_percent = 2.0, .memory_bytes = 400, .controllable = false},
        {.pid = 22, .executable_path = L"c:\\apps\\beta.exe", .display_name = L"beta.exe",
         .cpu_percent = 0.25, .memory_bytes = 50, .controllable = true},
        {.pid = 23, .display_name = L"mystery.exe", .cpu_percent = 3.0, .memory_bytes = 75, .controllable = false},
    };
    const auto groups = build_process_groups(group_processes);
    const auto alpha = std::find_if(groups.begin(), groups.end(), [](const ProcessGroup& group) {
        return group.executable_path == L"c:\\apps\\alpha.exe";
    });
    expect(alpha != groups.end(), "same executable path groups into one app row");
    if (alpha != groups.end()) {
        expect(alpha->process_count == 2 && alpha->available_count == 1, "group counts total and available processes");
        expect(alpha->cpu_percent == 3.5 && alpha->memory_bytes == 500, "group aggregates CPU and committed memory");
        expect(alpha->state == L"Partial", "mixed controllability produces partial group state");
        expect(alpha->largest_memory_process_index == 1, "largest-memory member is drill-in target");
    }
    const auto mystery = std::find_if(groups.begin(), groups.end(), [](const ProcessGroup& group) {
        return group.display_name == L"mystery.exe";
    });
    expect(mystery != groups.end() && !mystery->has_target_path && mystery->state == L"Path unavailable",
           "pathless processes group by display name and cannot become cap targets");

    RuleEngine state_engine;
    state_engine.set_rules({outer});
    state_engine.reconcile({});
    expect(state_engine.statuses().contains(L"outer"), "reconcile creates a rule status");
    state_engine.set_paused(true);
    expect(state_engine.statuses().at(L"outer").state == RuleState::paused,
           "pausing immediately updates enabled rule status");
    state_engine.set_rules({});
    expect(state_engine.statuses().empty(), "removed rules also remove stale statuses");

    const DWORD integrity = current_process_integrity_rid();
    expect(integrity >= SECURITY_MANDATORY_LOW_RID, "current process integrity can be inspected");
    const auto launched = UnelevatedLauncher::launch_suspended(helper_path);
    const bool safe_launcher_available = launched.error.empty();
    if (!safe_launcher_available) {
        expect(integrity > SECURITY_MANDATORY_MEDIUM_RID,
               "safe launcher may be unavailable when no medium-integrity shell token exists");
    } else if (launched.process) {
        expect(process_integrity_rid(launched.process) <= SECURITY_MANDATORY_MEDIUM_RID,
               "safe launcher never creates an elevated target");
        TerminateProcess(launched.process, 0);
        ResumeThread(launched.thread);
        WaitForSingleObject(launched.process, 5000);
    }

    Rule launch_rule{};
    launch_rule.id = L"launch-" + create_rule_id();
    launch_rule.executable_path = normalize_executable_path(helper_path.wstring());
    launch_rule.display_name = L"Test helper";
    launch_rule.cpu_enabled = true;
    launch_rule.cpu_percent = 50;
    RuleEngine launch_engine;
    launch_engine.set_rules({launch_rule});
    const auto launch_result = launch_engine.launch_limited(launch_rule.id);
    if (safe_launcher_available) {
        expect(launch_result.error.empty() && launch_result.pid != 0,
               "engine launches, assigns, and resumes a limited process");
    } else {
        expect(!launch_result.error.empty() && launch_result.pid == 0,
               "engine reports when limited launch is unavailable");
    }
    if (launch_result.pid) {
        const HANDLE limited = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, launch_result.pid);
        if (limited) {
            TerminateProcess(limited, 0);
            WaitForSingleObject(limited, 5000);
            CloseHandle(limited);
        }
    }
    launch_engine.shutdown();

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
