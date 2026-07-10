# Phase 2: Threshold and Pyramid Resource Views

This change extends the non-owning explicit-state Processing path to threshold visualization and pyramid processing.

## Added entry points

- `D3D12ThresholdProcessor::RecordThresholdView()`
- `D3D12ThresholdProcessor::RecordRangeThresholdView()`
- `D3D12ThresholdProcessor::RecordConfidenceHeatmapView()`
- `D3D12ThresholdProcessor::RecordClassColorMapView()`
- `D3D12ThresholdProcessor::RecordMaskOverlayView()`
- `D3D12PyramidProcessor::RecordDownsample2xView()`
- `D3D12PyramidProcessor::RecordUpsample2xView()`
- `D3D12PyramidBlur::RecordPyramidBlurView()`
- `D3D12PyramidRegionBlur::RecordPyramidRegionBlurView()`

Every view entry point requires explicit before/after states. Existing owned-resource methods remain unchanged and are not overloaded.

## Borrowed workspace types

Pyramid blur uses multiple intermediate textures, so the public API includes non-owning workspace views:

- `D3D12PyramidBlurWorkspaceView`
- `D3D12PyramidRegionBlurWorkspaceView`

The view types contain only metadata and `D3D12ResourceView` values. They do not retain COM references. Constructing a workspace view from an owned workspace does not transfer ownership.

## Workspace state descriptions

- `D3D12PyramidBlurWorkspaceStateDesc` describes initial and final states for every downsample texture, blur scratch texture, low-resolution blurred texture, and upsample texture.
- `D3D12PyramidBlurStateDesc` adds source and destination states.
- `D3D12PyramidRegionBlurStateDesc` adds the region-blur intermediate and final destination states.

The vector lengths must exactly match their corresponding workspace vectors. Null resources and resource aliasing across source, destination, and workspace entries are rejected before command recording.

## Implementation model

The complex pyramid view paths create temporary `D3D12Resource` wrappers around borrowed pointers without calling `AddRef`. Initial states are copied into the temporary state caches, the existing tracked-state implementation records the internal multi-pass barriers, and a final barrier batch restores every resource to its requested final state. All borrowed pointers are detached before temporary wrappers are destroyed, including during exception unwinding.

The external owner must keep every referenced resource alive until submitted GPU work has completed.

## Compatibility

Compile-time tests preserve the original unique signatures of all threshold, pyramid primitive, pyramid blur, and pyramid region blur methods published before this change.
