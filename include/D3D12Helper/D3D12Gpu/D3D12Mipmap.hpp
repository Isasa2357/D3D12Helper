#pragma once
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

struct D3D12MipLevelInfo {
    UINT width = 0;
    UINT height = 0;
};

UINT CalculateMipLevelCount(UINT width, UINT height) noexcept;
D3D12MipLevelInfo GetMipLevelInfo(UINT baseWidth, UINT baseHeight, UINT mipLevel);
bool IsMipLevelValid(UINT baseWidth, UINT baseHeight, UINT mipLevel) noexcept;
bool IsMipmappedTexture(const D3D12Resource& texture) noexcept;

D3D12Resource CreateMipmappedTexture2D(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT16 mipLevels = 0,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

void ValidateMipmappedTexture(
    const D3D12Resource& texture,
    const char* functionName,
    UINT minMipLevels = 2);

} // namespace D3D12CoreLib
