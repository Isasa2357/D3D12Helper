# D3D12Binding

D3D12Binding provides small helpers for descriptor heap and root parameter binding.

## Added in v1.4.0

- D3D12DescriptorHeapSet
- D3D12BindingSet
- Descriptor table helpers
- 32-bit root constant helpers
- Root CBV / SRV / UAV helpers

Unlike D3D11, D3D12 does not need scoped unbind helpers in the same sense. Binding is explicit command-list state.

Use `D3D12BindingSet::BindCompute` or `D3D12BindingSet::BindGraphics` after setting the matching root signature and pipeline state.
