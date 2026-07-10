# Phase 2 ResourceView expansion: Blur and Mask

This increment extends the non-owning Processing path to multi-pass blur and mask operations.

## Added entry points

- `D3D12Blurrer::RecordBlurView()`
- `D3D12RegionBlur::RecordRegionBlurView()`
- `D3D12MaskProcessor::RecordApplyMaskView()`
- `D3D12MaskProcessor::RecordBlendByMaskView()`
- `D3D12MaskProcessor::RecordCombineMasksView()`
- `D3D12MaskProcessor::RecordInvertMaskView()`

All entry points take `D3D12ResourceView`, do not call `AddRef` or `Release`, and require explicit before/after resource states.

## Blur state rules

`RecordBlurView()` uses `D3D12ProcessingBlurStateDesc` and requires explicit states for:

- source
- intermediate scratch texture
- destination

The source, scratch, and destination resources must be different objects. The underlying owned-resource implementation already validates this rule.

`RecordRegionBlurView()` uses `D3D12ProcessingRegionBlurStateDesc` and requires explicit states for:

- source
- blur scratch texture
- blurred intermediate texture
- destination

All four resources must be mutually distinct. The intermediate state transitions performed between the horizontal blur, vertical blur, and region blend remain explicit and do not update an external owner's state cache.

## Mask state rules

Apply and combine use `D3D12ProcessingTwoInputStateDesc`, blend uses `D3D12ProcessingThreeInputStateDesc`, and invert uses `D3D12ProcessingStateDesc`.

The current owned mask implementation emits one transition entry per input. Therefore, the non-owning mask APIs reject aliases between input views instead of creating duplicate transition barriers for the same resource. Destination aliasing is already rejected by the owned implementation.

## Compatibility

The original methods remain unchanged and are not overloaded:

- `RecordBlur()`
- `RecordRegionBlur()`
- `RecordApplyMask()`
- `RecordBlendByMask()`
- `RecordCombineMasks()`
- `RecordInvertMask()`

Compile-time tests preserve their v1.12.1 member-function signatures.

## Lifetime

The external owner must keep every referenced resource alive until all submitted GPU work that uses the resource has completed. The temporary adapters exist only during command recording and detach borrowed pointers during normal return and exception unwinding.
