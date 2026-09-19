# HardCap — Status & Roadmap
_Portfolio audit: 2026-07-11_

## What this is
A portable Windows 11 utility that applies OS-enforced hard CPU and committed-memory ceilings to
desktop applications using Windows Job Objects. Native C++ (~2.5k lines) with a clean layered
layout: `src/core/` (rules + persistence), `src/engine/` (rule engine, process grouping),
`src/platform/` (job controller, process monitor, unelevated launcher, handle wrappers), and
`src/ui/` (main window, tray). CMake presets + ctest (`tests/`), CI on `windows-2022`
(`.github/workflows/ci.yml`), and tag-driven releases publishing `HardCap-windows-x64.zip` with
`SHA256SUMS.txt` (`.github/workflows/release.yml`, process in `docs/RELEASE.md`).

## Current state
v0.3.1 released 2026-06-29 — the most recently active project (7 commits, 4 tags, all since
2026-06-23). The working tree is clean and repo hygiene is the best of the portfolio: CHANGELOG,
SECURITY/SUPPORT/CONTRIBUTING/CoC, issue templates, and user docs (`docs/USER_GUIDE.md`,
`docs/ARCHITECTURE.md`). Shipped functionality: CPU caps (1-100% of machine), aggregate memory
ceilings per app group, a Task-Manager-like Apps view with rule state badges (Enforced / Partial /
Disabled), attach to running processes, "Launch limited" for full group coverage, saved
per-executable rules auto-reapplied on process start, and tray pause/resume/exit-with-removal.

Gaps and loose ends:
- Unsigned binary: SmartScreen warnings on every download (acknowledged in README) — the main
  adoption blocker.
- HardCap must stay running for limits to exist, but there is no start-with-Windows option, so
  saved rules silently stop applying after reboot until manually launched.
- Attach to already-jobbed processes is best-effort/partial by design; nested job support isn't
  explored.
- Tests exist (core, monitoring, launcher, persistence, UI smoke in `tests/`) but the suite is
  thin relative to the failure modes (race between rule apply and process exit, settings
  migration, permission edge cases).
- No screenshots in README and no winget/Scoop distribution.

## Definition of "finished"
A 1.0: signed (or reputation-established) portable exe plus winget manifest, start-with-Windows
support so rules survive reboots, robust attach/launch behavior documented per edge case, a
settings-schema versioning story for `%LOCALAPPDATA%\HardCap\settings.json`, README screenshots,
and test coverage over the rule engine's trickiest paths. Feature scope can stay exactly as-is —
this tool wins by being small and trustworthy.

## Roadmap

### Phase 1 — Now (next 1-2 weeks)
- Add "Start with Windows" (elevated scheduled task created/removed from the tray or a settings
  toggle in `src/ui/main_window.cpp`), since rules are only enforced while HardCap runs.
- Add screenshots/GIF of the Apps view and rule editor to `README.md` — biggest
  conversion-per-hour improvement available.
- Pursue signing: evaluate Azure Trusted Signing vs. an OV cert; wire signing into
  `.github/workflows/release.yml` once available. Until then, document the SmartScreen bypass
  steps with screenshots in `docs/USER_GUIDE.md`.
- Publish a winget manifest for the portable zip to make installs one command.

### Phase 2 — Next (2-6 weeks)
- Harden `src/engine/rule_engine.cpp` + `src/platform/process_monitor.cpp` edge cases with tests:
  process exits mid-apply, rules for executables on removable drives, rapid start/stop storms,
  and settings files from future versions (schema version field + migration in
  `src/core/rule_repository.cpp`).
- Improve partial-attach UX: when attach is partial, offer a one-click "restart app limited" flow
  building on the existing clean-launch guard from v0.3.0.
- Add per-rule notes/status history so "Partial" states are explainable (which PID failed and why,
  extending the v0.3.0 attention-status work).
- Cut v0.4.0 with the above through the existing tag-driven release process.

### Phase 3 — Later (optional/stretch)
- Optional caps beyond CPU/memory that Job Objects support cleanly (active process count,
  UI-restricted tokens); evaluate I/O rate control (`JOBOBJECT_IO_RATE_CONTROL_INFORMATION`).
- Scheduled/conditional rules (e.g., cap only on battery or during work hours).
- Localization pass and a proper app icon set beyond `tools/GenerateIcon.ps1`.
- A short technical blog post on Job Object gotchas — strong portfolio artifact for this repo.

## Effort to "finished"
M (1-4 weeks) part-time solo — the core is released and tested; 1.0 is autostart, signing or
distribution reputation, docs polish, and edge-case hardening rather than new subsystems.
