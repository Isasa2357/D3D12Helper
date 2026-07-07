# D3D12Helper v1.2.0 Release Notes

## Summary

v1.2.0 adds CPU-side texture transfer helpers.

## Added

- D3D12CpuImage
- D3D12TextureTransfer
- Transfer CTest suite

## Scope

The first transfer baseline supports synchronous Texture2D upload, update, full readback, and region readback for simple single-plane formats.

File and media I/O remain out of scope.
