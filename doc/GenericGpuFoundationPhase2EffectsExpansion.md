# Generic GPU Foundation Phase 2: Processing Effects Expansion

This change extends the non-owning `D3D12ResourceView` Processing path to three additional single-pass effects:

- `D3D12ColorAdjuster::RecordColorAdjustView()`
- `D3D12KernelFilter::RecordKernelFilterView()`
- `D3D12RegionEffectProcessor::RecordRegionEffectView()`

## Ownership and lifetime

The view APIs do not call `AddRef` or `Release`. The external owner must keep every referenced resource alive until all submitted GPU work that uses the resource has completed.

The temporary internal adapter is scoped to the call and detaches the borrowed pointer before destruction, including during exception unwinding.

## Resource states

Every view API requires `D3D12ProcessingStateDesc::useExplicitStates == true`.

The caller provides:

- source state before the pass
- source state after the pass
- destination state before the pass
- destination state after the pass

The view path does not read or update the independent state cache of any owned `D3D12Resource` wrapper.

## Compatibility

The v1.12.1 methods remain unchanged and unambiguous:

- `RecordColorAdjust()`
- `RecordKernelFilter()`
- `RecordRegionEffect()`

The new entry points use distinct `...View` names so existing address-of expressions and overload resolution are preserved.

## Tests

The `ResourceView` suite verifies:

- explicit-state enforcement
- command recording and execution for all three processors
- no mutation of owned-wrapper state caches

The `CompatibilityV1121` suite verifies the original method signatures at compile time.
