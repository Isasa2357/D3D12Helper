# D3D12Helper v1.12.0 Release Notes

## Summary

v1.12.0 hardens `D3D12FusedProcessor` test coverage.

## Added

- Dedicated `ProcessingFusedPipeline` CTest suite.
- Shader compile coverage for fused RGB and YUV420 resize shaders.
- Runtime readback coverage for RGBA point resize through `RecordConvertResize`.
- Validation coverage for invalid fused output sizes.
