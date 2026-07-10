#pragma once
//
// D3D12ResourceCreate.hpp
// Detailed committed Buffer / Texture2D creation without replacing the
// v1.12.1 convenience APIs in D3D12Helpers.hpp.
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <optional>

namespace D3D12CoreLib {

struct D3D12BufferCreateDesc {
    UINT64 sizeBytes = 0;
    UINT64 alignment = 0;

    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CPU_PAGE_PROPERTY cpuPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    D3D12_MEMORY_POOL memoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    UINT creationNodeMask = 1;
    UINT visibleNodeMask = 1;

    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
};

struct D3D12Texture2DCreateDesc {
    UINT64 width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT16 arraySize = 1;
    UINT16 mipLevels = 1;
    DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };
    D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    UINT64 alignment = 0;

    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CPU_PAGE_PROPERTY cpuPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    D3D12_MEMORY_POOL memoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    UINT creationNodeMask = 1;
    UINT visibleNodeMask = 1;

    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    std::optional<D3D12_CLEAR_VALUE> clearValue;
};

// Distinct names intentionally preserve unambiguous address-of expressions for
// the v1.12.1 CreateBuffer / CreateTexture2D function signatures.
// These functions create committed resources only; placed and reserved
// resources remain outside this API.
D3D12Resource CreateBufferDetailed(
    D3D12Core& core,
    const D3D12BufferCreateDesc& desc);

D3D12Resource CreateTexture2DDetailed(
    D3D12Core& core,
    const D3D12Texture2DCreateDesc& desc);

} // namespace D3D12CoreLib
