# Changelog

All notable changes to D3D12Helper are documented here.

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

### Notes

D3D12Framework remains the compatibility layer for v1.x users. It should not be removed during v1.1.0 module normalization.
