# User Guide

## Creating a Limit

1. Start HardCap and accept the administrator prompt.
2. Select a process in the list, or choose **Add executable...**.
3. Enable CPU, memory, or both.
4. Choose **Save & apply**.

Saved rules are app group caps keyed by executable path. When a matching process or child process appears, HardCap assigns the process tree to the rule's Job Object when Windows allows it.

## Comparing Apps

Use **Apps** to see Task Manager-style grouped totals for running processes that share an executable path. Select an Apps row with a path to create or edit the app group cap. Double-click an app row to jump to its largest committed-memory process in **Running** for inspection.

## Launching an App With Limits

After saving a rule, choose **Launch limited** to start the selected executable directly inside its Job Object. HardCap launches the target without carrying HardCap's elevated integrity level into the child process.

For full app group coverage, close existing matching processes before **Launch limited**. If matching processes are already running, HardCap blocks limited launch and asks you to close them first. **Save & apply** remains best-effort for already-running processes and can show **Partial** when Windows refuses assignment.

## Pausing Limits

Use the tray menu to pause and resume all limits. Pausing lifts active limits while keeping saved rules. Resuming reconciles rules with running processes again.

## Removing Limits

Disable or remove a rule from the main window to lift that rule's active limits. Choose **Exit and remove limits** from the tray menu before closing HardCap completely.

## Troubleshooting

- If a process is marked unavailable, Windows denied access, the process is protected, the process is critical, or the process is HardCap itself.
- If an app group cap is **Partial**, at least one matching process could not be assigned to the Job Object. Close the app completely and use **Launch limited** for full group coverage.
- If a target exits under a memory cap, the app may not handle allocation failure gracefully.
- If rules disappear after startup, inspect `%LOCALAPPDATA%\HardCap\` for an invalid settings backup.
