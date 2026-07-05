#include "D3D12Processing/D3D12ProcessingContext.hpp"

#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

D3D12_FEATURE_DATA_FORMAT_SUPPORT QueryFormatSupport(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT data = {};
    data.Format = format;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof(data)))) {
        data.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
        data.Support2 = D3D12_FORMAT_SUPPORT2_NONE;
    }
    return data;
}

bool HasSupport1(D3D12_FORMAT_SUPPORT1 value, D3D12_FORMAT_SUPPORT1 flag) noexcept {
    return (static_cast<UINT>(value) & static_cast<UINT>(flag)) == static_cast<UINT>(flag);
}

bool HasSupport2(D3D12_FORMAT_SUPPORT2 value, D3D12_FORMAT_SUPPORT2 flag) noexcept {
    return (static_cast<UINT>(value) & static_cast<UINT>(flag)) == static_cast<UINT>(flag);
}

bool SupportsTypedUavStore(ID3D12Device* device, DXGI_FORMAT format) {
    const auto support = QueryFormatSupport(device, format);
    return HasSupport1(support.Support1, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
           HasSupport2(support.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
}

bool SupportsTextureLoad(ID3D12Device* device, DXGI_FORMAT format) {
    const auto support = QueryFormatSupport(device, format);
    return HasSupport1(support.Support1, D3D12_FORMAT_SUPPORT1_TEXTURE2D) &&
           HasSupport1(support.Support1, D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
}

} // namespace

void D3D12ProcessingContext::Initialize(
    D3D12Core& core,
    D3D12DescriptorAllocator* cbvSrvUavAllocator,
    D3D12DescriptorAllocator* samplerAllocator,
    std::filesystem::path shaderDirectory) {

    if (!core.GetDevice()) {
        throw ValidationError("D3D12ProcessingContext::Initialize: core has no device");
    }
    if (!cbvSrvUavAllocator) {
        throw ValidationError("D3D12ProcessingContext::Initialize: null CBV/SRV/UAV allocator");
    }

    m_core = &core;
    m_cbvSrvUavAllocator = cbvSrvUavAllocator;
    m_samplerAllocator = samplerAllocator;

    if (shaderDirectory.empty()) {
        shaderDirectory = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    }
    m_shaderDirectory = std::move(shaderDirectory);
    m_caps = QueryCaps(core.GetDevice());
}

D3D12Core& D3D12ProcessingContext::Core() const {
    if (!m_core) {
        throw ValidationError("D3D12ProcessingContext::Core: context is not initialized");
    }
    return *m_core;
}

ID3D12Device* D3D12ProcessingContext::GetDevice() const {
    return Core().GetDevice();
}

D3D12DescriptorAllocator& D3D12ProcessingContext::CbvSrvUavAllocator() const {
    if (!m_cbvSrvUavAllocator) {
        throw ValidationError("D3D12ProcessingContext::CbvSrvUavAllocator: context is not initialized");
    }
    return *m_cbvSrvUavAllocator;
}

D3D12DescriptorAllocator& D3D12ProcessingContext::SamplerAllocator() const {
    if (!m_samplerAllocator) {
        throw ValidationError("D3D12ProcessingContext::SamplerAllocator: no sampler allocator was provided");
    }
    return *m_samplerAllocator;
}

D3D12ProcessingCaps D3D12ProcessingContext::QueryCaps(ID3D12Device* device) const {
    if (!device) {
        throw ValidationError("D3D12ProcessingContext::QueryCaps: null device");
    }

    D3D12ProcessingCaps caps = {};

    caps.rgba8Uav = SupportsTypedUavStore(device, DXGI_FORMAT_R8G8B8A8_UNORM);
    caps.bgra8Uav = SupportsTypedUavStore(device, DXGI_FORMAT_B8G8R8A8_UNORM);
    caps.rgba16FloatUav = SupportsTypedUavStore(device, DXGI_FORMAT_R16G16B16A16_FLOAT);

    caps.nv12Srv = SupportsTextureLoad(device, DXGI_FORMAT_NV12);
    caps.p010Srv = SupportsTextureLoad(device, DXGI_FORMAT_P010);

    const auto r8 = QueryFormatSupport(device, DXGI_FORMAT_R8_UNORM);
    caps.r8TypedUavLoad = HasSupport2(r8.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
    caps.r8TypedUavStore = HasSupport2(r8.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

    const auto rg8 = QueryFormatSupport(device, DXGI_FORMAT_R8G8_UNORM);
    caps.rg8TypedUavLoad = HasSupport2(rg8.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
    caps.rg8TypedUavStore = HasSupport2(rg8.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

    const auto r16 = QueryFormatSupport(device, DXGI_FORMAT_R16_UNORM);
    caps.r16TypedUavLoad = HasSupport2(r16.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
    caps.r16TypedUavStore = HasSupport2(r16.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

    const auto rg16 = QueryFormatSupport(device, DXGI_FORMAT_R16G16_UNORM);
    caps.rg16TypedUavLoad = HasSupport2(rg16.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
    caps.rg16TypedUavStore = HasSupport2(rg16.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

    const auto nv12 = QueryFormatSupport(device, DXGI_FORMAT_NV12);
    caps.nv12Uav = HasSupport1(nv12.Support1, D3D12_FORMAT_SUPPORT1_TEXTURE2D) &&
                   HasSupport1(nv12.Support1, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
                   caps.r8TypedUavStore && caps.rg8TypedUavStore;

    const auto p010 = QueryFormatSupport(device, DXGI_FORMAT_P010);
    caps.p010Uav = HasSupport1(p010.Support1, D3D12_FORMAT_SUPPORT1_TEXTURE2D) &&
                   HasSupport1(p010.Support1, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
                   caps.r16TypedUavStore && caps.rg16TypedUavStore;

    return caps;
}

} // namespace Processing
} // namespace D3D12CoreLib
