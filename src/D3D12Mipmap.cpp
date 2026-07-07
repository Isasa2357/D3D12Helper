#include <D3D12Helper/D3D12Gpu/D3D12Mipmap.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace D3D12CoreLib {

UINT CalculateMipLevelCount(UINT width, UINT height) noexcept {
    if (width == 0 || height == 0) return 0;
    UINT levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
        ++levels;
    }
    return levels;
}

D3D12MipLevelInfo GetMipLevelInfo(UINT baseWidth, UINT baseHeight, UINT mipLevel) {
    const UINT maxLevels = CalculateMipLevelCount(baseWidth, baseHeight);
    if (maxLevels == 0) throw std::runtime_error("GetMipLevelInfo: width and height must be > 0");
    if (mipLevel >= maxLevels) throw std::runtime_error("GetMipLevelInfo: mipLevel is out of range");
    D3D12MipLevelInfo info;
    info.width = baseWidth;
    info.height = baseHeight;
    for (UINT i = 0; i < mipLevel; ++i) {
        info.width = std::max(1u, info.width / 2u);
        info.height = std::max(1u, info.height / 2u);
    }
    return info;
}

bool IsMipLevelValid(UINT baseWidth, UINT baseHeight, UINT mipLevel) noexcept {
    return mipLevel < CalculateMipLevelCount(baseWidth, baseHeight);
}

bool IsMipmappedTexture(const D3D12Resource& texture) noexcept {
    return texture.Get() && texture.GetDesc().MipLevels > 1;
}

D3D12Resource CreateMipmappedTexture2D(D3D12Core& core, UINT width, UINT height, DXGI_FORMAT format, UINT16 mipLevels, D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags) {
    if (width == 0 || height == 0) throw std::runtime_error("CreateMipmappedTexture2D: width and height must be > 0");
    if (format == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("CreateMipmappedTexture2D: format must not be UNKNOWN");
    const UINT fullLevels = CalculateMipLevelCount(width, height);
    const UINT16 resolvedMipLevels = (mipLevels == 0) ? static_cast<UINT16>(fullLevels) : mipLevels;
    if (resolvedMipLevels == 0 || resolvedMipLevels > fullLevels) throw std::runtime_error("CreateMipmappedTexture2D: invalid mipLevels");
    return CreateTexture2D(core, width, height, format, initialState, flags, 1, resolvedMipLevels);
}

void ValidateMipmappedTexture(const D3D12Resource& texture, const char* functionName, UINT minMipLevels) {
    const char* fn = functionName ? functionName : "ValidateMipmappedTexture";
    if (!texture.Get()) throw std::runtime_error(std::string(fn) + ": null texture");
    const auto desc = texture.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) throw std::runtime_error(std::string(fn) + ": texture is not Texture2D");
    if (desc.MipLevels < minMipLevels) throw std::runtime_error(std::string(fn) + ": texture does not have enough mip levels");
    const UINT maxLevels = CalculateMipLevelCount(static_cast<UINT>(desc.Width), desc.Height);
    if (desc.MipLevels > maxLevels) throw std::runtime_error(std::string(fn) + ": texture has invalid mip level count");
}

} // namespace D3D12CoreLib
