# Changelog

All notable changes to HardCap are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project uses semantic versioning while it is pre-1.0.

## [0.3.1] - 2026-06-29

### Changed

- Disable Launch limited for disabled rules instead of allowing a click that only reports an enable-first error.

## [0.3.0] - 2026-06-29

### Added

- App group cap workflow from Apps rows backed by executable paths.
- Clean-launch guard for full app group caps when matching processes are already running.
- Clear Add executable flow for saving closed-app rules before launching them limited.

### Changed

- Apps rows with saved rules now show rule state such as Enforced, Partial, or Disabled.
- Partial assignment failures now use attention status and include the failing PID.
- Dark-theme checkbox labels are rendered through readable static labels.

## [0.2.0] - 2026-06-29

### Added

- Task Manager-like Apps view with executable-path grouping, aggregate CPU, aggregate committed memory, process counts, and drill-in to the largest member process.
- Release checksum publishing with `SHA256SUMS.txt` beside the Windows zip artifact.

### Changed

- Renamed process memory display to Committed to better explain Task Manager differences.
- Hardened limit input parsing, rule pause/remove state handling, Win32 handle ownership, and release workflow permissions.

## [0.1.0] - 2026-06-23

### Added

- Initial portable Windows release.
- CPU hard caps using Windows Job Object CPU rate control.
- Aggregate committed-memory ceilings using Windows Job Object memory limits.
- Saved executable rules with automatic reapplication to matching process starts.
- Rule editor for running processes and manually selected executables.
- Tray menu for show, pause/resume, and exit-with-limit-removal actions.
- Core, process-monitoring, launcher, persistence, and smoke tests.
