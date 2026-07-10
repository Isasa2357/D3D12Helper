# Generic GPU Foundation Phase 2 Audit

This audit records the completion boundary for non-owning D3D12 resource access in the Processing layer.

## Coverage

Every public Processing method that records GPU work from caller-supplied `D3D12Resource` objects now has a separately named `D3D12ResourceView` entry point.

- Format conversion: `RecordConvertView`
- Fused conversion and resize: `RecordConvertResizeView`
- Resize: `RecordResizeView`
- Remap and undistort map: `RecordRemapView`, `RecordApplyUndistortMapView`
- Composite: `RecordCompositeView`
- Color adjustment: `RecordColorAdjustView`
- Kernel filter: `RecordKernelFilterView`
- Region effect: `RecordRegionEffectView`
- Blur and region blur: `RecordBlurView`, `RecordRegionBlurView`
- Mask processing: apply, blend, combine, and invert view paths
- Threshold processing: threshold, range threshold, confidence heatmap, class color map, and mask overlay view paths
- Pyramid primitives: downsample and upsample view paths
- Pyramid blur and pyramid region blur: borrowed workspace view paths
- Advanced processing: affine transform, perspective transform, 3D LUT, and undistort-map view paths

Resource-creation methods remain owning APIs by design. Context initialization, shader-cache access, and capability queries do not accept caller-supplied resources and therefore need no resource-view counterpart.

## State model

All non-owning Processing entry points require explicit before and after states. They do not read or mutate the state cache stored in an independently owned `D3D12Resource` wrapper.

Multi-pass workspace APIs require state arrays matching the workspace resource arrays. Null resources, inconsistent metadata, and resource aliases that would cause duplicate or contradictory transitions are rejected before command recording.

## Ownership model

`D3D12ResourceView` stores one raw `ID3D12Resource*` and never calls `AddRef` or `Release`.

Processing adapters temporarily expose a borrowed pointer through the existing owned-resource implementation. The pointer is installed into an initially empty wrapper without `AddRef`, then detached before wrapper destruction, including exception unwinding.

The `ResourceView` test suite probes real COM reference counts before and after:

- view construction and destruction,
- resource validation,
- barrier aggregation,
- AdvancedProcessing command recording and execution.

The expected count is unchanged after each completed operation.

## Compatibility

Existing `Record*` methods keep their original names and signatures. View paths use distinct `*View` names so address-of expressions for the existing methods remain unambiguous. `CompatibilityV1121` compile-time tests cover the original signatures, including AdvancedProcessing.

## Completion condition

Phase 2 is complete when the updated `ResourceView`, `CompatibilityV1121`, `AdvancedProcessing`, and full Debug/Release CTest runs pass on the user Windows environment.
