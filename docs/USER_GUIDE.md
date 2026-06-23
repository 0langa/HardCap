# User Guide

## Creating a Limit

1. Start HardCap and accept the administrator prompt.
2. Select a process in the list, or choose **Add executable...**.
3. Enable CPU, memory, or both.
4. Choose **Save & apply**.

Saved rules match by executable path. When a matching process or child process appears, HardCap assigns the process tree to the rule's Job Object.

## Launching an App With Limits

After saving a rule, choose **Launch limited** to start the selected executable directly inside its Job Object. HardCap launches the target without carrying HardCap's elevated integrity level into the child process.

## Pausing Limits

Use the tray menu to pause and resume all limits. Pausing lifts active limits while keeping saved rules. Resuming reconciles rules with running processes again.

## Removing Limits

Disable or remove a rule from the main window to lift that rule's active limits. Choose **Exit and remove limits** from the tray menu before closing HardCap completely.

## Troubleshooting

- If a process is marked unavailable, Windows denied access, the process is protected, the process is critical, or the process is HardCap itself.
- If a target exits under a memory cap, the app may not handle allocation failure gracefully.
- If rules disappear after startup, inspect `%LOCALAPPDATA%\HardCap\` for an invalid settings backup.
