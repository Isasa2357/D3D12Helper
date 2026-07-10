# Generic GPU Foundation: Final Audit

This document records the final implementation status of the requested generic D3D12 foundation work across Phases 1 through 4.

## Scope

The completed scope is the D3D12Helper generic foundation. It does not include a video encoder/decoder implementation, codec policy, muxing, or a general automatic resource-state tracker.

The implementation is arranged as a dependent pull-request chain:

1. `feature/generic-gpu-foundation-phase1` -> `main`
2. `feature/non-owning-resource-view` -> Phase 1 branch
3. `feature/typed-command-list-foundation` -> Phase 2 branch
4. `feature/yuv-hlsl-primitives` -> Phase 3 branch

The pull requests must therefore be merged in this order unless the commits are rebased or combined first.

## Final requirement status

| Requirement | Status | Implemented surface |
|---|---|---|
| Range-specific readback mapping | Complete | `D3D12ReadbackBuffer::MapRead`, move-only `D3D12MappedReadRange`, bounds/empty-range/conflict handling, RAII unmap |
| Queue synchronization point | Complete | `D3D12QueueSyncPoint`, `SignalPoint`, `GpuWaitPoint`, `CpuWaitPoint` |
| Detailed resource creation | Complete | `D3D12BufferCreateDesc`, `D3D12Texture2DCreateDesc`, `CreateBufferDetailed`, `CreateTexture2DDetailed` |
| Non-owning resource access | Complete | `D3D12ResourceView`, validation/view creation helpers, non-owning explicit-state paths for every public Processing `Record*` operation that accepts caller resources |
| Resource validation | Complete | aggregate-result and throwing Texture2D validation, size/format/flags/device/multiple constraints |
| Barrier aggregation | Complete | `D3D12BarrierBatch` with Transition, UAV, and Aliasing barriers |
| Typed command allocator/list foundation | Complete | `D3D12CommandAllocatorContext`, `CreateTypedCommandList<TCommandList>`, existing `D3D12CommandContext` internally shared with the new foundation |
| Reusable YUV HLSL primitives | Complete | NV12/P010, BT.601/709/2020, Full/Limited range, point/linear sampling, luma/chroma stores, CPU/GPU golden tests |

## Phase 1 audit

### Readback mapping

`MapRead(offset, size)` maps only the caller-declared CPU read range and returns a move-only RAII object. The implementation rejects overflow and out-of-bounds ranges, permits an empty range, prevents simultaneous legacy/scoped mappings, and unmaps with a written range of `{0, 0}`.

The pre-existing `Map()` and `Unmap()` methods and their legacy destruction/move behavior remain available.

### Queue synchronization

`D3D12QueueSyncPoint` packages a signaled fence and value. It supports CPU waiting and queue-to-queue GPU waiting without adding an overload that would make the existing `GpuWait(ID3D12Fence*, UINT64)` address ambiguous.

The synchronization point retains the fence with `ComPtr`. This is an intentional safety choice rather than a borrowed-fence design.

### Detailed resource creation

The detailed APIs expose committed-resource controls including heap properties, node masks, heap/resource flags, alignment, initial state, texture sample/layout values, and optional clear values.

The APIs use the distinct names `CreateBufferDetailed` and `CreateTexture2DDetailed`. This intentionally preserves source code such as `&CreateBuffer` and `&CreateTexture2D`.

### Resource validation and barriers

Texture2D validation supports both aggregate results and exceptions. `D3D12BarrierBatch` is deliberately state-tracker-free and command-list-agnostic.

## Phase 2 audit

`D3D12ResourceView` is a raw, non-owning `ID3D12Resource*` view. It does not call `AddRef` or `Release` and carries no cached state.

Every public Processing method that records GPU work from caller-provided resources has a separately named `*View` path. These paths require explicit before/after states and do not mutate the state cache of another owned `D3D12Resource` wrapper.

Coverage includes:

- format conversion and fused conversion/resize,
- resize, remap, composite, and undistortion,
- color adjustment, kernel filtering, and region effects,
- blur and region blur,
- mask and threshold operations,
- pyramid primitives and borrowed multi-resource workspaces,
- affine/perspective transforms and 3D LUT processing.

Tests measure actual COM reference counts around view construction, validation, barrier use, and GPU command recording/execution. The expected count remains unchanged.

## Phase 3 audit

`D3D12CommandAllocatorContext` separates allocator ownership from the selected command-list interface.

`CreateTypedCommandList<TCommandList>` accepts either a raw allocator or the typed allocator context. The context overload rejects command-list-type mismatches before calling D3D12. The raw overload relies on D3D12 runtime validation because `ID3D12CommandAllocator` exposes no type query.

D3D12Helper itself does not depend on `d3d12video.h`; an upper library may include the SDK header and instantiate the template with a specialized interface such as `ID3D12VideoDecodeCommandList`.

The existing `D3D12CommandContext` public API remains unchanged and now delegates internally to the shared allocator/list foundation.

## Phase 4 audit

`YuvPrimitives.hlsli` provides reusable format-aware functions for application-owned fused shaders.

Supported paths include:

- NV12 8-bit and P010 10-bit code/storage conversion,
- BT.601, BT.709, and BT.2020,
- Full and Limited range,
- point and linear YUV420 sampling during resize,
- point and linear logical-RGBA sampling,
- YUV420 luma and chroma plane stores.

P010 explicitly maps a 10-bit video code to bits 15:6 of an R16/R16G16 plane word. CPU golden vectors, GPU probe output, actual NV12 plane bytes, and actual P010 high-bit words are tested.

Existing shader filenames, descriptor layout, root-constant layout, C++ APIs, and the previous `ColorSpace.hlsli` function behavior are preserved.

## Compatibility audit

The work preserves the v1.12.1 source surface:

- no existing public type, function, method signature, return type, or header path was removed or renamed,
- new alternatives use distinct names when a same-name overload could make address-of expressions ambiguous,
- legacy readback mapping remains available,
- existing Processing methods remain unique and unmodified,
- existing `D3D12CommandContext` signatures remain unchanged,
- `CompatibilityV1121` compile-time tests cover the affected public surface.

## Validation status

The user Windows environment completed Debug and Release builds and full CTest runs after each phase. The final Phase 4 run passed after correcting only the storage-test readback-range calculation to use:

```text
(rows - 1) * rowPitch + rowSizeInBytes
```

The correction was limited to the test. The readback implementation, product C++ code, and YUV HLSL primitives were not weakened to accept an invalid range.

The relevant Processing and typed-command-list samples also completed successfully.

## Completion boundary

All originally identified generic D3D12 foundation requirements are implemented and validated on the feature-branch chain.

Intentionally excluded work remains:

- codec, GOP, rate control, bitstream, mux, or container writing,
- video encode/decode session management,
- a global automatic resource-state tracker,
- command allocator pooling/frame scheduling,
- D3D11Helper feature-parity work unrelated to this D3D12 request.

These are separate higher-level or future foundation projects and are not blockers for this request.