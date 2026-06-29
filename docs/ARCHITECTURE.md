# Architecture

HardCap is a small Win32 desktop app split into four layers.

## App and UI

`src/app/main.cpp` owns process startup and single-instance behavior. `src/ui/` owns the main window, rule editor, process list, and notification-area menu.

The UI keeps the current rule set in memory, saves it through `RuleRepository`, and asks `RuleEngine` to reconcile saved rules with the latest process snapshot. **Apps** rows are grouped process summaries and can create the same executable-path app group caps as **Running** and **Rules**.

## Core Rules

`src/core/` defines the rule model, validation, executable-path normalization, rule identifiers, and JSON persistence.

Rules are persisted as UTF-8 JSON in `%LOCALAPPDATA%\HardCap\settings.json`. The parser accepts the project schema and moves invalid settings aside rather than failing startup.

## Engine

`src/engine/rule_engine.*` maps executable-path rules to matching processes and owns one active `JobController` per rule. It assigns the outermost matching process tree to the rule's Job Object, applies aggregate CPU and committed-memory ceilings, observes Job Object events, and lifts limits when rules are disabled, paused, removed, or the app exits. Attaching already-running processes is best-effort; full app group coverage depends on launching the executable inside the Job Object before its children start.

## Platform

`src/platform/` wraps the Windows APIs that need careful lifetime handling:

- `JobController` creates named Job Objects, applies CPU and memory limits, assigns processes, and listens for completion-port events.
- `ProcessMonitor` snapshots process metadata, CPU, memory, session, and controllability, and uses WMI to notice process starts.
- `UnelevatedLauncher` starts targets from the shell token when HardCap itself is elevated.

## Tests

`tests/test_main.cpp` covers rule validation, persistence, app group rule updates, Job Object enforcement, process snapshots, WMI watcher lifecycle, process-tree assignment, launch readiness, partial assignment status, and unelevated launch behavior. `tests/ui_smoke_main.cpp` builds the UI surface as a smoke target.
