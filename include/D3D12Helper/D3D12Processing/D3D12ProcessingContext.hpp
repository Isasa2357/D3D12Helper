#pragma once
//
// D3D12ProcessingContext.hpp
// Shared state and capability cache for the D3D12 Processing Layer.
//
#include "D3D12ProcessingTypes.hpp"
#include "../D3D12Core/D3D12Core.hpp"
#include "../D3D12Framework/D3D12DescriptorAllocator.hpp"

#include <filesystem>

namespace D3D12CoreLib {
namespace Processing {

class D3D12ProcessingContext {
public:
    void Initialize(
        D3D12Core& core,
        D3D12DescriptorAllocator* cbvSrvUavAllocator,
        D3D12DescriptorAllocator* samplerAllocator,
        std::filesystem::path shaderDirectory = {});

    D3D12Core& Core() const;
    ID3D12Device* GetDevice() const;

    D3D12DescriptorAllocator& CbvSrvUavAllocator() const;
    D3D12DescriptorAllocator& SamplerAllocator() const;

    const std::filesystem::path& ShaderDirectory() const noexcept { return m_shaderDirectory; }
    const D3D12ProcessingCaps& Caps() const noexcept { return m_caps; }

    bool SupportsNv12Srv() const noexcept { return m_caps.nv12Srv; }
    bool SupportsNv12Uav() const noexcept { return m_caps.nv12Uav; }
    bool SupportsP010Srv() const noexcept { return m_caps.p010Srv; }
    bool SupportsP010Uav() const noexcept { return m_caps.p010Uav; }
    bool SupportsRgba8Uav() const noexcept { return m_caps.rgba8Uav; }
    bool SupportsBgra8Uav() const noexcept { return m_caps.bgra8Uav; }
    bool SupportsRgba16FloatUav() const noexcept { return m_caps.rgba16FloatUav; }

private:
    D3D12ProcessingCaps QueryCaps(ID3D12Device* device) const;

    D3D12Core* m_core = nullptr;
    D3D12DescriptorAllocator* m_cbvSrvUavAllocator = nullptr;
    D3D12DescriptorAllocator* m_samplerAllocator = nullptr;
    std::filesystem::path m_shaderDirectory;
    D3D12ProcessingCaps m_caps = {};
};

} // namespace Processing
} // namespace D3D12CoreLib
