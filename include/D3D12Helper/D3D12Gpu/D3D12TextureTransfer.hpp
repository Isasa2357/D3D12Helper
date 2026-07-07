#pragma once
//
// D3D12TextureTransfer.hpp - Texture2D <-> D3D12CpuImage transfer helpers.
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12CpuImage.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

D3D12CpuImage ReadbackTexture2DToCpuImage(D3D12Core& core, const D3D12Resource& srcTexture);
D3D12CpuImage ReadbackTexture2DRegionToCpuImage(D3D12Core& core, const D3D12Resource& srcTexture, const D3D12_BOX& srcBox);

D3D12Resource CreateTexture2DFromCpuImage(
    D3D12Core& core,
    const D3D12CpuImage& image,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void UpdateTexture2DFromCpuImage(
    D3D12Core& core,
    D3D12Resource& dstTexture,
    const D3D12CpuImage& image,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

} // namespace D3D12CoreLib
