# D3D12Helper v1.4.0 Release Notes

## Summary

v1.4.0 adds D3D12 binding helpers.

## Added

- D3D12DescriptorHeapSet
- D3D12BindingSet
- Descriptor table helpers
- 32-bit root constant helpers
- Root CBV / SRV / UAV helpers
- Binding CTest suite

## Notes

D3D12 binding is explicit command-list state. This release does not add D3D11-style scoped unbind because D3D12 does not require the same hazard model.
