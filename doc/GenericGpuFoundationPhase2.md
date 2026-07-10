# Generic GPU Foundation Phase 2

Phase 2 adds an explicit non-owning path for D3D12 resources supplied by camera SDKs, runtimes, swapchains, inference systems, and other external owners.

## `D3D12ResourceView`

`D3D12Gpu/D3D12ResourceView.hpp` provides a pointer-sized view of `ID3D12Resource`.

- It never calls `AddRef` or `Release`.
- It stores no resource state.
- It is trivially copyable and trivially destructible.
- The external owner must keep the resource alive through every CPU operation and all submitted GPU work that uses it.
- It can be constructed explicitly from either `ID3D12Resource*` or an owned `D3D12Resource`.

The absence of state is intentional. A separate mutable state value inside each borrowed view could diverge when multiple views refer to the same resource or when another API changes the resource state.

## Layering

`D3D12ResourceView` belongs to `D3D12Gpu`. `D3D12Core` remains independent of Layer 2 types.

Core barrier APIs continue to accept raw `ID3D12Resource*`. A view is used with them through `view.Get()`:

```cpp
D3D12BarrierBatch batch;
batch.Transition(
    view.Get(),
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATE_COPY_SOURCE);
```

This avoids a reverse dependency from `D3D12Core` to `D3D12Gpu`.

## Validation

The Phase 1 raw-pointer validation API remains unchanged. Distinct entry points accept the non-owning view:

- `ValidateTexture2DView()`
- `ValidateTexture2DViewOrThrow()`

Distinct names avoid introducing overload ambiguity into the already tested Phase 1 API.

## Processing

The first Processing integration covers the paths most directly useful for camera and video pipelines:

- `D3D12FormatConverter::RecordConvertView()`
- `D3D12FusedProcessor::RecordConvertResizeView()`
- RGBA and YUV420 descriptor creation from `D3D12ResourceView`

The existing owned-resource methods remain unchanged and continue to support their single-state cache.

The view methods require:

```cpp
state.useExplicitStates = true;
```

The caller must provide `srcBefore`, `srcAfter`, `dstBefore`, and `dstAfter`. Processing records the requested barriers but never mutates an external state cache.

The existing method names are not overloaded with view arguments. Separate `*View` names preserve unambiguous expressions such as:

```cpp
auto fn = &D3D12FormatConverter::RecordConvert;
auto fusedFn = &D3D12FusedProcessor::RecordConvertResize;
```

## Compatibility

- No v1.12.1 public type, function, parameter, return type, or include path is removed or changed.
- Existing Processing entry points remain uniquely addressable.
- Existing owned-resource behavior remains available.
- New APIs do not contain encoder, codec, Media Foundation, or NVENC-specific concepts.

## Current scope

This phase does not yet add view entry points to every Processing processor. Format conversion and fused convert/resize are implemented first because they are the primary boundary operations for camera, inference, and video pipelines. Remaining processors can be migrated after this state/lifetime model passes Debug and Release testing.
