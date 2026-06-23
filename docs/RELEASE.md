# Release Process

HardCap releases are built by GitHub Actions from version tags.

## Versioning

The project version is declared in `CMakeLists.txt`:

```cmake
project(HardCap VERSION 0.1.0 LANGUAGES CXX RC)
```

For a release:

1. Update the project version.
2. Update `CHANGELOG.md`.
3. Confirm local verification:

   ```powershell
   cmake --preset windows-x64
   cmake --build --preset debug
   ctest --preset debug
   cmake --build --preset release
   ```

4. Commit the release changes.
5. Tag the commit:

   ```powershell
   git tag v0.1.0
   git push origin main --tags
   ```

The release workflow publishes `HardCap.exe` as a downloadable artifact.

## Signing

HardCap is not currently code signed. Do not describe unsigned binaries as trusted or verified. Add signing to the release workflow before claiming publisher identity in release notes or docs.
