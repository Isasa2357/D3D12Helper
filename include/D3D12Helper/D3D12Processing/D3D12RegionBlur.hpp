#pragma once
//
// D3D12RegionBlur.hpp
// Region-masked blur built from D3D12Blurrer + original/blurred mask blend.
//
#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

struct D3D12ProcessingRegionBlurStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES blurScratchBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurScratchAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES blurredBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurredAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter = D3D12_RESOURCE_STATE_COMMON;

    bool useExplicitStates = false;
};

class D3D12RegionBlur {
public:
    D3D12RegionBlur();
    ~D3D12RegionBlur();

    D3D12RegionBlur(const D3D12RegionBlur&) = delete;
    D3D12RegionBlur& operator=(const D3D12RegionBlur&) = delete;
    D3D12RegionBlur(D3D12RegionBlur&&) noexcept;
    D3D12RegionBlur& operator=(D3D12RegionBlur&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    // Runs blur into blurred, then blends original src and blurred according to RegionBlurDesc.
    // blurScratch and blurred must be different SRV+UAV RGBA-like textures.
    void RecordRegionBlur(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& blurScratch,
        D3D12Resource& blurred,
        D3D12Resource& dst,
        const RegionBlurDesc& desc,
        const D3D12ProcessingRegionBlurStateDesc& state = {});

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    D3D12Resource CreateScratchTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    D3D12Resource CreateBlurredTexture(
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
    D3D12Blurrer m_blurrer;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
