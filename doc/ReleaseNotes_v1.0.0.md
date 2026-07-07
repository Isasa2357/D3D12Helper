# D3D12Helper v1.0.0 Release Notes

## Summary

v1.0.0 fixes the current D3D12Helper implementation as the first stable baseline.

It keeps the existing Layer 1 / Layer 2 / Layer 3 structure:

- D3D12Core
- D3D12Framework
- D3D12Processing

## Scope

This release includes the existing core, framework, processing, sample, and test implementations.

The next version, v1.1.0, should reorganize the public module layout toward:

- D3D12Foundation
- D3D12Core
- D3D12Gpu
- D3D12Presentation
- D3D12Processing
- D3D12Interop
- D3D12Diagnostics

D3D12Framework should remain available as a v1.x compatibility wrapper.

## Validation

Recommended validation command:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64
cmake --build out/build/default --config Debug --parallel
ctest --test-dir out/build/default -C Debug --output-on-failure
```
