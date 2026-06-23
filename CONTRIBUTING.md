# Contributing

Thanks for taking the time to improve HardCap.

## Development Setup

Install:

- Windows 11 x64.
- CMake 3.25 or newer.
- Visual Studio 2022 or Visual Studio 2022 Build Tools with the Desktop development with C++ workload.

Build and test:

```powershell
cmake --preset windows-x64
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
```

## Pull Requests

- Keep changes focused and describe the user-visible behavior.
- Add or update tests for rule validation, persistence, Job Object behavior, or process-monitoring changes.
- Update `README.md`, `CHANGELOG.md`, or files in `docs/` when behavior, requirements, or release steps change.
- Do not commit generated build output from `build/`.

## Code Style

- C++20.
- MSVC warnings enabled with `/W4 /permissive- /EHsc /utf-8`.
- Prefer standard library types and small platform wrappers over broad abstractions.
- Keep Win32 handle ownership explicit and easy to audit.
