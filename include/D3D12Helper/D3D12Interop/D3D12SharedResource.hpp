#pragma once
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>
#include <D3D12Helper/D3D12Interop/D3D12SharedHandle.hpp>

namespace D3D12CoreLib {

D3D12SharedHandle CreateSharedHandleForResource(ID3D12Device* device, const D3D12Resource& resource, LPCWSTR name = nullptr);
D3D12Resource OpenSharedResource(ID3D12Device* device, HANDLE handle, D3D12_RESOURCE_STATES assumedState = D3D12_RESOURCE_STATE_COMMON);
D3D12Resource CreateSharedTexture2DResource(D3D12Core& core, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, UINT16 arraySize = 1, UINT16 mipLevels = 1);
D3D12SharedHandle CreateSharedHandleForFence(ID3D12Device* device, const D3D12Fence& fence, LPCWSTR name = nullptr);
D3D12Fence OpenSharedFence(ID3D12Device* device, HANDLE handle);

} // namespace D3D12CoreLib
