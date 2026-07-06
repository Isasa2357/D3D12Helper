//
// D3D12SharedResource.cpp
//
#include <D3D12Helper/D3D12Core/D3D12SharedResource.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>

namespace D3D12CoreLib {

HANDLE D3D12SharedResource::CreateSharedHandle(
    ID3D12Device* device,
    ID3D12Resource* resource,
    LPCWSTR name) {

    HANDLE handle = nullptr;
    D3D12CORE_THROW_IF_FAILED_MSG(
        device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, name, &handle),
        "CreateSharedHandle failed (resource must be created with D3D12_HEAP_FLAG_SHARED)");
    return handle;
}

ComPtr<ID3D12Resource> D3D12SharedResource::OpenSharedHandle(
    ID3D12Device* device,
    HANDLE handle) {

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(
        device->OpenSharedHandle(handle, IID_PPV_ARGS(&resource)));
    return resource;
}

ComPtr<ID3D12Resource> D3D12SharedResource::OpenSharedTexture2D(
    ID3D12Device* device,
    HANDLE handle) {

    ComPtr<ID3D12Resource> resource = OpenSharedHandle(device, handle);
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error(
            "D3D12SharedResource::OpenSharedTexture2D: resource is not a Texture2D");
    }
    return resource;
}

} // namespace D3D12CoreLib
