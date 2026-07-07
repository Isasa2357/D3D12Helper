# D3D12Helper v1.9.0 Release Notes

## Summary

v1.9.0 adds packaging and install support.

## Added

- Install rules for the D3D12Helper target.
- Installed public headers.
- Installed processing shader assets.
- CMake package config files.
- Exported `D3D12Helper::D3D12Helper` target for `find_package` users.
- Packaging documentation.

## Notes

Install rules are enabled by default only when D3D12Helper is configured as the top-level project. When consumed by `add_subdirectory` or `FetchContent`, install rules are disabled by default.
