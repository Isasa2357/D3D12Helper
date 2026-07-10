#pragma once
//
// D3D12RegionEffect.hpp
// Region-based one-pass effects for RGBA-like textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12RegionEffectProcessor {
public:
    D3D12RegionEffectProcessor();
    ~D3D12RegionEffectProcessor();

    D3D12RegionEffectProcessor(const D3D12RegionEffectProcessor&) = delete;
    D3D12RegionEffectProcessor& operator=(const D3D12RegionEffectProcessor&) = delete;
    D3D12RegionEffectProcessor(D3D12RegionEffectProcessor&&) noexcept;
    D3D12RegionEffectProcessor& operator=(D3D12RegionEffectProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordRegionEffect(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const RegionEffectDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    // Non-owning path. The caller must provide explicit before/after states and
    // keep both resources alive until the submitted GPU work completes.
    void RecordRegionEffectView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const RegionEffectDesc& desc,
        const D3D12ProcessingStateDesc& state);

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

private:
    struct Pipelines;

    void EnsureInitialized() const;
    void EnsurePipelines();

    D3D12ProcessingContext* m_context = nullptr;
    D3D12ProcessingShaderCache m_shaderCache;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
