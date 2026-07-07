# D3D12Helper v1.5.0 Release Notes

## Summary

v1.5.0 adds diagnostics helpers.

## Added

- Device removed helpers
- InfoQueue wrapper
- GPU timer
- GPU profiler
- Diagnostics CTest suite

## Notes

Existing D3D12Debug helpers remain available. GPU timing helpers use timestamp queries and require the caller to execute and wait before reading results.
