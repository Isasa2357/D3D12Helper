//
// D3D12Subresource.cpp
//
#include "D3D12Subresource.hpp"

namespace D3D12CoreLib {

UINT CalcSubresource(
    UINT mipSlice,
    UINT arraySlice,
    UINT planeSlice,
    UINT mipLevels,
    UINT arraySize) noexcept {
    return mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize;
}

} // namespace D3D12CoreLib
