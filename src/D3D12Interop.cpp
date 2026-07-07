#include <D3D12Helper/D3D12Interop/D3D12SharedResource.hpp>
#include <D3D12Helper/D3D12Core/D3D12SharedResource.hpp>

#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {

D3D12SharedHandle CreateSharedHandleForResource(ID3D12Device* device, const D3D12Resource& resource, LPCWSTR name) {
    if (!device) throw std::runtime_error("CreateSharedHandleForResource: null device");
    if (!resource.Get()) throw std::runtime_error("CreateSharedHandleForResource: null resource");
    return D3D12SharedHandle(D3D12SharedResource::CreateSharedHandle(device, resource.Get(), name));
}

D3D12Resource OpenSharedResource(ID3D12Device* device, HANDLE handle, D3D12_RESOURCE_STATES assumedState) {
    if (!device) throw std::runtime_error("OpenSharedResource: null device");
    if (!IsValidSharedHandle(handle)) throw std::runtime_error("OpenSharedResource: invalid handle");
    auto resource = D3D12SharedResource::OpenSharedHandle(device, handle);
    return D3D12Resource(std::move(resource), assumedState);
}

D3D12Resource CreateSharedTexture2DResource(D3D12Core& core, UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags, UINT16 arraySize, UINT16 mipLevels) {
    return CreateSharedTexture2D(core, width, height, format, initialState, flags, arraySize, mipLevels);
}

D3D12SharedHandle CreateSharedHandleForFence(ID3D12Device* device, const D3D12Fence& fence, LPCWSTR name) {
    if (!device) throw std::runtime_error("CreateSharedHandleForFence: null device");
    return D3D12SharedHandle(fence.CreateSharedHandle(device, name));
}

D3D12Fence OpenSharedFence(ID3D12Device* device, HANDLE handle) {
    if (!device) throw std::runtime_error("OpenSharedFence: null device");
    if (!IsValidSharedHandle(handle)) throw std::runtime_error("OpenSharedFence: invalid handle");
    D3D12Fence fence;
    fence.OpenSharedHandle(device, handle);
    return fence;
}

} // namespace D3D12CoreLib
