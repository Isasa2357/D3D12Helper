# D3D12Helper v1.11.0 Release Notes

## Summary

v1.11.0 hardens Advanced Processing coverage around `D3D12PyramidRegionBlur`.

## Added

- Dedicated `ProcessingPyramidRegionBlur` CTest suite.
- Shader compile coverage for `PyramidDownsample2xRgba.hlsl`, `PyramidUpsample2xRgba.hlsl`, Gaussian blur shaders, and `RegionBlurBlendRgba.hlsl`.
- Runtime coverage for circle/outside pyramid region blur.
- Validation coverage for invalid circle radius.

## Notes

`D3D12PyramidRegionBlur` combines `D3D12PyramidBlur` with a region mask blend pass. It is intended for large-radius region blur effects where full-resolution blur is too expensive.
