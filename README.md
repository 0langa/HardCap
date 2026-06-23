# HardCap

HardCap is a portable Windows 11 utility for applying hard CPU and committed-memory ceilings to desktop applications. It uses Windows Job Objects, so limits are enforced by the operating system while HardCap is running.

## Highlights

- Apply CPU hard caps from 1% to 100% of total machine capacity.
- Apply aggregate committed-memory ceilings to a process tree.
- Attach limits to already-running processes, or launch an app directly into a limited Job Object.
- Save per-executable rules and automatically reapply them to future process starts.
- Pause, resume, and remove all active limits from the notification area.
- Ships as a portable executable with a statically linked Visual C++ runtime.

## Requirements

- Windows 11 x64.
- Administrator approval when HardCap starts.
- CMake 3.25 or newer for local builds.
- Visual Studio 2022 or Visual Studio 2022 Build Tools with the Desktop development with C++ workload.

## Download

Release builds are published from GitHub Actions. Download the latest `HardCap.exe` from the [Releases](../../releases) page.

HardCap is currently unsigned. Windows SmartScreen may show a warning until the project has signing infrastructure and reputation.

## Use

1. Run `HardCap.exe` and accept the administrator prompt.
2. Select a running process, or choose **Add executable...** to create a rule for an app that is not running yet.
3. Enable **CPU hard cap**, **Memory hard cap**, or both.
4. Enter the ceiling and choose **Save & apply**.
5. Leave HardCap running in the notification area so saved rules can be applied to future process starts.

CPU values are percentages of total machine capacity. For example, `25` means one quarter of the whole computer, not one quarter of a single core.

Memory values are aggregate committed-memory ceilings for the matching process and descendants. Reaching a memory ceiling can make the limited app fail allocations or exit.

Closing the main window minimizes HardCap to the notification area. Use **Exit and remove limits** from the tray menu to lift active caps before HardCap exits.

## Build

```powershell
cmake --preset windows-x64
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
```

The release executable is written to `build\dist\HardCap.exe`. Debug test binaries are written under `build\Debug`.

## Configuration

Rules are stored in:

```text
%LOCALAPPDATA%\HardCap\settings.json
```

Invalid settings files are moved aside with an `.invalid-<timestamp>.json` suffix and HardCap starts with an empty rule set.

## Safety Model

HardCap cannot limit critical Windows processes, protected processes, inaccessible processes, or itself. Some applications may not behave gracefully when memory allocation is denied by a Job Object limit; start with conservative limits for important work.

HardCap only enforces limits while it is running. Exiting through **Exit and remove limits** lifts active caps first.

## Project Status

HardCap is early release software. The core limiting path, rule persistence, process monitoring, and portable release build are covered by automated tests, but the app is not yet code signed.

See [CHANGELOG.md](CHANGELOG.md) for release notes and [docs/RELEASE.md](docs/RELEASE.md) for the release process.
