# D3D12Helper v1.12.0 Release Notes

## Theme

Advanced Processing.

This release starts the v1.12.0 Processing expansion with explicit matrix-based transform passes, 3D LUT application, and an undistort-map application facade.

## Added

- Added `D3D12AdvancedProcessor`.
- Added affine transform API:
  - `AffineTransformDesc`
  - `D3D12AdvancedProcessor::RecordAffineTransform`
- Added perspective / homography transform API:
  - `PerspectiveTransformDesc`
  - `D3D12AdvancedProcessor::RecordPerspectiveTransform`
- Added 3D LUT API:
  - `Lut3DDesc`
  - `D3D12AdvancedProcessor::RecordApplyLut3D`
- Added undistort-map application API:
  - `D3D12AdvancedProcessor::RecordApplyUndistortMap`
  - This is a thin facade over the existing `D3D12Remapper` path and uses `RemapDesc` / `R32G32_FLOAT` maps.
- Added compute shaders:
  - `shaders/D3D12Processing/AdvancedTransformRgba.hlsl`
  - `shaders/D3D12Processing/ApplyLut3D.hlsl`
- Added test suite:
  - `AdvancedProcessing`
  - Shader compilation tests
  - Public default tests
  - Initialization / output texture creation tests
  - Golden runtime tests for affine identity, perspective identity, and 3D LUT identity

## Scope

The current implementation supports RGBA-like source and destination textures. Transform coordinates are destination-local pixel coordinates mapped to source-local pixel coordinates. `RecordApplyLut3D` expects a `Texture3D` LUT with RGB domain `[0, 1]` and performs manual trilinear interpolation.

## Non-goals for this first v1.12.0 step

- D3D11 parity implementation. This should follow after the D3D12 API shape is validated.
- Exhaustive transform coverage such as rotation / scale / arbitrary homography golden cases.
- 1D LUT / 2D strip LUT variants.
- Automatic generation of undistort maps from camera calibration parameters.
