#pragma once
//
// D3D12Subresource.hpp - mip / array / plane から subresource index を計算するヘルパ
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

UINT CalcSubresource(
    UINT mipSlice,
    UINT arraySlice,
    UINT planeSlice,
    UINT mipLevels,
    UINT arraySize) noexcept;

} // namespace D3D12CoreLib
