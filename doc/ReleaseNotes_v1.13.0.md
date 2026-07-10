# D3D12Helper v1.13.0 Release Notes

## Summary

v1.13.0 adds the generic GPU foundation required by external-resource processing, multi-queue synchronization, specialized command lists, and application-owned fused YUV shaders while preserving the v1.x public source surface.

This release includes the work merged through PRs #25, #26, #27, and #28.

## Added

### Readback and synchronization

- `D3D12ReadbackBuffer::MapRead(offset, size)`.
- Move-only RAII `D3D12MappedReadRange`.
- `D3D12QueueSyncPoint`.
- `D3D12Queue::SignalPoint()`.
- `D3D12Queue::GpuWaitPoint()` and `CpuWaitPoint()`.
- `D3D12BarrierBatch` for Transition, UAV, and Aliasing barriers.

### Detailed resource creation and validation

- `D3D12BufferCreateDesc` and `D3D12Texture2DCreateDesc`.
- `CreateBufferDetailed()` and `CreateTexture2DDetailed()`.
- Aggregate and throwing Texture2D validation APIs.
- `D3D12ResourceView`, a non-owning `ID3D12Resource*` view with no `AddRef` / `Release` behavior.

### Non-owning Processing paths

Every public Processing recording operation that accepts caller resources now has a separately named `*View` path using explicit before/after states.

Coverage includes:

- format conversion and fused conversion/resize,
- resize, remap, composite, and undistortion,
- color adjustment, kernel filtering, region effects,
- blur and region blur,
- mask and threshold operations,
- pyramid primitives and borrowed pyramid workspaces,
- affine/perspective transforms and 3D LUT processing.

### Typed command-list foundation

- `D3D12CommandAllocatorContext`.
- `CreateTypedCommandList<TCommandList>()` for raw allocators and allocator contexts.
- Direct, Compute, Copy, and SDK-provided specialized command-list interfaces.
- Existing `D3D12CommandContext` internally shares the new allocator/list foundation.

### Reusable YUV HLSL primitives

- `shaders/D3D12Processing/YuvPrimitives.hlsli`.
- NV12 8-bit and P010 10-bit code/storage handling.
- BT.601, BT.709, and BT.2020.
- Full and Limited range.
- Point and linear YUV420/RGBA resize sampling.
- NV12/P010 luma and chroma store helpers.
- CPU golden vectors, GPU probe tests, and actual plane-storage readback tests.

### Samples and documentation

- `sample/19_TypedCommandList`.
- Updated `sample/18_ProcessingCustomFusedShader` to use `YuvPrimitives.hlsli`.
- Phase-specific design documents and `GenericGpuFoundationFinalAudit.md`.

## Compatibility

- No existing public type, function, method signature, return type, or header path was removed or renamed.
- New alternatives use distinct names where a same-name overload could make address-of expressions ambiguous.
- Legacy `D3D12ReadbackBuffer::Map()` / `Unmap()` remain available.
- Existing Processing `Record*` methods remain unchanged.
- Existing `D3D12CommandContext` signatures remain unchanged.
- Existing `ColorSpace.hlsli` public functions and numeric behavior remain unchanged.
- `CompatibilityV1121` compile-time tests cover the affected public surface.

## Validation

The Windows validation environment completed:

- Debug and Release builds,
- the complete CTest suite,
- `D3D12Sample_07_ProcessingFusedConvertResize`,
- `D3D12Sample_08_ProcessingP010Rgba16`,
- `D3D12Sample_18_ProcessingCustomFusedShader`,
- `D3D12Sample_19_TypedCommandList`.

## Intentionally out of scope

- codec, GOP, rate control, bitstream, mux, or container writing,
- video encode/decode session management,
- a global automatic resource-state tracker,
- command allocator pooling and frame scheduling.
