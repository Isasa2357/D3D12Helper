# D3D12Helper v1.7.0 Release Notes

## Summary

v1.7.0 adds view descriptor and resource state helper APIs.

## Added

- D3D12View
- D3D12State
- ViewState CTest suite

## Notes

D3D12View creates descriptor descriptions and validates descriptor handles. It does not allocate descriptor heap entries by itself.

D3D12State records transitions using the existing single-state D3D12Resource tracking model. Full per-subresource state tracking is intentionally left to caller-side explicit management.
