# Changelog

All notable changes to D3D12Helper are documented here.

## v1.10.1 - Shader Reflection hardening

### Summary

v1.10.1 adds DXIL/container bytecode support to shader reflection.

### Changed

- `ReflectShaderBytecode` now first tries `D3DReflect` for DXBC bytecode.
- If `D3DReflect` fails, reflection falls back to `IDxcUtils::CreateReflection` for DXIL/container bytecode.
- Added a DXC/DXIL reflection test that is skipped when DXC is unavailable.

## v1.10.0 - Shader Reflection

### Summary

v1.10.0 adds lightweight shader reflection helpers for compiled shader bytecode.

### Added

- `D3D12ShaderReflection.hpp`.
- `ReflectShaderBytecode` for DXBC shader bytecode.
- Resource binding inspection.
- Constant buffer and variable inspection.
- Input/output signature parameter inspection.
- Input layout element generation from vertex shader signatures.
- Compute shader thread group size inspection.
- Shader reflection tests.

## v1.9.2 - Shader asset namespace hardening

### Summary

v1.9.2 hardens runtime shader asset placement so D3D11Helper and D3D12Helper can be used by the same application without HLSL filename collisions.

### Changed

- Runtime sample/test shader copies now use `D3D12Helper/shaders/...` instead of a flat `shaders/...` root.
- `D3D12ProcessingContext` now prefers `D3D12Helper/shaders/D3D12Processing` for default shader discovery.
- The legacy `shaders/D3D12Processing` default path remains as a fallback for existing applications.
- `D3D12HelperConfig.cmake` now exposes `D3D12Helper_PROCESSING_SHADER_DIR`.
- Package smoke tests validate that installed shader variables are namespaced.

## v1.9.1 - Packaging hardening

### Summary

v1.9.1 adds install-tree consumer smoke tests and package metadata hardening.

### Added

- `PackageSmoke` CTest entry.
- Minimal installed-package consumer project.
- Install / find_package / consumer-build smoke script.

### Changed

- `D3D12HelperConfig.cmake` now exposes `D3D12Helper_VERSION` and `D3D12Helper_SHADER_DIR`.

## v1.9.0 - Packaging

### Summary

v1.9.0 adds install/export package support for CMake consumers.

### Added

- Install rules for library, headers, and shader assets.
- CMake package config files.
- Exported `D3D12Helper::D3D12Helper` package target.
- Packaging documentation.

## v1.8.1 - Test hardening

### Summary

v1.8.1 hardens tests and documents coverage.

### Added

- Hardening tests.
- Test coverage document.

## v1.8.0 - Interop

### Summary

v1.8.0 adds shared handle, shared resource, and shared fence wrappers.

### Added

- D3D12SharedHandle.
- Shared resource helpers.
- Shared fence helpers.
- Interop tests.

## v1.7.0 - View / State

### Summary

v1.7.0 adds view descriptor and resource state helpers.

### Added

- D3D12View helpers.
- D3D12State helpers.
- View / State tests.

## v1.6.0 - Copy / Resolve / Mipmap

### Summary

v1.6.0 adds copy, resolve, and mipmap utility helpers.

### Added

- D3D12Copy helpers.
- D3D12Resolve helpers.
- D3D12Mipmap helpers.
- Copy / Resolve / Mipmap tests.

## v1.5.0 - Diagnostics

### Summary

v1.5.0 adds diagnostics helpers.

### Added

- Device removed helpers.
- InfoQueue wrapper.
- GPU timer.
- GPU profiler.
- Diagnostics tests.

## v1.4.0 - Binding

### Summary

v1.4.0 adds D3D12 binding helpers for descriptor heaps and root parameters.

### Added

- D3D12DescriptorHeapSet.
- D3D12BindingSet.
- Descriptor table binding helpers.
- 32-bit root constant binding helpers.
- Root CBV / SRV / UAV binding helpers.
- Binding tests.

## v1.3.0 - Presentation

### Summary

v1.3.0 adds presentation-layer wrappers.

### Added

- D3D12RenderTarget.
- D3D12SwapChain.
- Presentation tests.

## v1.2.0 - Transfer

### Summary

v1.2.0 adds CPU-side texture transfer helpers.

### Added

- D3D12CpuImage.
- D3D12TextureTransfer.
- Transfer tests.

## v1.1.0 - Module layout normalization

### Summary

v1.1.0 adds the D3D11Helper-style public module layout without moving existing implementation files.

The release introduces canonical umbrella headers for:

- D3D12Foundation
- D3D12Gpu
- D3D12Presentation
- D3D12Interop
- D3D12Diagnostics

D3D12Core and D3D12Processing remain canonical modules. D3D12Framework remains available as the v1.x compatibility wrapper.

### Added

- Added D3D12Foundation wrapper headers.
- Added D3D12Gpu umbrella header.
- Added D3D12Presentation umbrella header.
- Added D3D12Interop umbrella header.
- Added D3D12Diagnostics umbrella header.
- Added module header smoke test suite.

### Changed

- Updated project version to 1.1.0.

## v1.0.0 - Stable baseline

### Summary

v1.0.0 fixes the current D3D12Helper implementation as the first stable baseline.

This release keeps the existing Layer 1 / Layer 2 / Layer 3 structure:

- D3D12Core
- D3D12Framework
- D3D12Processing

Future v1.1.0 work will reorganize the public module layout toward the same category structure used by D3D11Helper:

- D3D12Foundation
- D3D12Core
- D3D12Gpu
- D3D12Presentation
- D3D12Processing
- D3D12Interop
- D3D12Diagnostics

### Included baseline

- Device, adapter, queue, fence, and command context helpers.
- Resource, descriptor, upload, readback, compute pipeline, graphics pipeline, shader compiler, and shared resource helpers.
- D3D12Processing processors for conversion, resize, remap, composite, blur, region effect, region blur, color adjust, kernel filter, mask, threshold, pyramid, pyramid blur, and fused processing.
- Existing samples and CTest suites.
