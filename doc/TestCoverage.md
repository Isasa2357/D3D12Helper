# D3D12Helper Test Coverage

This document summarizes the registered CTest suites as of v1.13.0.

All `test_*.cpp` files are linked into one `d3d12helper_tests` executable. CTest invokes the executable with a suite name so each subsystem can be run independently.

## Core / Foundation

- `ModuleHeaders`
- `FormatUtil`
- `DxgiUtil`
- `ThrowIfFailed`
- `Barrier`
- `Subresource`
- `Core`
- `Fence`
- `SharedFence`
- `CommandContext`
- `TypedCommandList`

`TypedCommandList` covers allocator lifecycle, Direct / Compute / Copy list creation, raw/context overloads, type mismatch rejection, null validation, fence-safe allocator reuse, and compile-time SDK-specialized interface support.

## GPU / Resource

- `DescriptorAllocator`
- `Resource`
- `ResourceCreateValidation`
- `ResourceView`
- `Helpers`
- `UploadReadback`
- `Transfer`
- `UploadRing`
- `ShaderCompiler`
- `ShaderReflection`
- `ComputePipeline`
- `GraphicsPipeline`
- `CopyResolveMipmap`
- `ViewState`
- `Binding`

`ResourceCreateValidation` covers detailed committed-resource descriptors and aggregate Texture2D validation. `ResourceView` covers non-owning resource access, descriptor/view creation, Processing recording, state-cache preservation, and real COM reference counts.

## Presentation / Diagnostics / Interop

- `Presentation`
- `Diagnostics`
- `Interop`

## Processing

- `Processing`
- `YuvHlslPrimitives`
- `ProcessingBlur`
- `ProcessingRegionEffect`
- `ProcessingRegionBlur`
- `ProcessingColorAdjust`
- `ProcessingKernelFilter`
- `ProcessingMask`
- `ProcessingThreshold`
- `ProcessingPyramid`
- `ProcessingPyramidBlur`
- `ProcessingPyramidRegionBlur`
- `AdvancedProcessing`

`YuvHlslPrimitives` covers:

- BT.601 / BT.709 / BT.2020 CPU golden values,
- Full / Limited range,
- NV12 8-bit and P010 10-bit code paths,
- P010 high-bit storage round trips,
- shader compilation after primitive integration,
- GPU probe comparison against CPU references,
- actual NV12/P010 plane-storage readback after RGBA conversion.

Processing suites use real GPU dispatch and readback where applicable. Tests skip only the cases requiring unsupported adapter capabilities such as a missing typed UAV store format.

## Compatibility / Hardening

- `Hardening`
- `CompatibilityV1121`
- `CoverageHardening`

`CompatibilityV1121` uses compile-time type checks to preserve existing public names, signatures, return types, and address-of expression uniqueness across the expanded API surface.

## Package validation

When both options are enabled:

```cmake
-D3D12HELPER_INSTALL=ON
-DD3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS=ON
```

CTest also registers `PackageSmoke`. It installs D3D12Helper into a temporary prefix and verifies a separate consumer project through `find_package(D3D12Helper CONFIG REQUIRED)`.

## Recommended release execution

```bat
ctest --test-dir out/build/release-v1.13.0 -C Debug --parallel --output-on-failure
ctest --test-dir out/build/release-v1.13.0 -C Release --parallel --output-on-failure
```
