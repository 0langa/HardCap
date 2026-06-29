# Release Process

HardCap releases are built by GitHub Actions from version tags.

## Versioning

The project version is declared in `CMakeLists.txt`:

```cmake
project(HardCap VERSION 0.3.0 LANGUAGES CXX RC)
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
   git tag -a v0.3.0 -m "HardCap v0.3.0"
   git push origin main --tags
   ```

The release workflow publishes `HardCap-windows-x64.zip` and `SHA256SUMS.txt` as downloadable artifacts.

Verify a downloaded release archive with:

```powershell
Get-FileHash .\HardCap-windows-x64.zip -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

The hash from `Get-FileHash` must match the hash listed in `SHA256SUMS.txt`.

## Signing

HardCap is not currently code signed. Checksums verify download integrity, not publisher identity. Do not describe unsigned binaries as trusted or verified. Add signing to the release workflow before claiming publisher identity in release notes or docs.
