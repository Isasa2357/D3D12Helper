# D3D12Helper v1.9.1 Release Notes

## Summary

v1.9.1 hardens packaging with install-tree consumer smoke tests.

## Added

- `D3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS` option.
- `PackageSmoke` CTest entry.
- Minimal installed-package consumer project under `test/package_smoke`.
- CMake script that runs install, configures a consumer with `find_package`, and builds it.

## Changed

- `D3D12HelperConfig.cmake` now exposes `D3D12Helper_VERSION` and `D3D12Helper_SHADER_DIR`.

## Notes

Package smoke tests are enabled by default only when D3D12Helper is the top-level CMake project. They are disabled by default when D3D12Helper is consumed through `add_subdirectory` or `FetchContent`.
