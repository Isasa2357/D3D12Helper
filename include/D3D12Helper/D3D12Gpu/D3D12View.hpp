#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorHandle.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

bool IsCpuDescriptorValid(D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept;
bool IsGpuDescriptorValid(D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept;
bool IsShaderVisibleDescriptor(const D3D12DescriptorHandle& handle) noexcept;

D3D12_SHADER_RESOURCE_VIEW_DESC MakeTexture2DSrvDesc(
    const D3D12Resource& texture,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mostDetailedMip = 0,
    UINT mipLevels = UINT_MAX);

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeTexture2DUavDesc(
    const D3D12Resource& texture,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

D3D12_RENDER_TARGET_VIEW_DESC MakeTexture2DRtvDesc(
    const D3D12Resource& texture,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

D3D12_DEPTH_STENCIL_VIEW_DESC MakeTexture2DDsvDesc(
    const D3D12Resource& texture,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0,
    D3D12_DSV_FLAGS flags = D3D12_DSV_FLAG_NONE);

D3D12_SHADER_RESOURCE_VIEW_DESC MakeBufferSrvDesc(
    const D3D12Resource& buffer,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeBufferUavDesc(
    const D3D12Resource& buffer,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

D3D12_CONSTANT_BUFFER_VIEW_DESC MakeConstantBufferViewDesc(
    const D3D12Resource& buffer,
    UINT64 byteOffset = 0,
    UINT sizeBytes = 0);

} // namespace D3D12CoreLib
