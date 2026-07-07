# D3D12Helper v1.6.0 Release Notes

## Summary

v1.6.0 adds copy, resolve, and mipmap utility helpers.

## Added

- D3D12Copy
- D3D12Resolve
- D3D12Mipmap
- Copy / Resolve / Mipmap CTest suite

## Notes

Copy and resolve helpers record D3D12 commands and perform basic state transitions using the existing single-state D3D12Resource tracking model.

Mipmap support in v1.6.0 provides mip-count, mip-size, validation, and mipmapped texture creation helpers. D3D12 does not provide D3D11-style automatic GenerateMips, so automatic mip generation can be added later using compute processing.
