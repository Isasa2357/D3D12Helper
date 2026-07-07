# Changelog

All notable changes to D3D12Helper are documented here.

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
