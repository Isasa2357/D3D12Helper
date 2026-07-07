# D3D12TextureTransfer

D3D12TextureTransfer provides synchronous Texture2D transfer helpers between D3D12 resources and CPU memory.

## Added in v1.2.0

- D3D12CpuImage
- ReadbackTexture2DToCpuImage
- ReadbackTexture2DRegionToCpuImage
- CreateTexture2DFromCpuImage
- UpdateTexture2DFromCpuImage

## Scope

The first transfer baseline supports simple Texture2D transfers:

- mip 0
- array slice 0
- single-plane formats
- non-compressed formats
- non-MSAA textures

File and media I/O are intentionally out of scope.
