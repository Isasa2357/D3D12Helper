#pragma once
//
// D3D12PyramidProcessor.hpp
// 2x downsample / 2x upsample primitives for RGBA-like D3D12 textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12PyramidProcessor {
public:
    D3D12PyramidProcessor();
    ~D3D12PyramidProcessor();

    D3D12PyramidProcessor(const D3D12PyramidProcessor&) = delete;
    D3D12PyramidProcessor& operator=(const D3D12PyramidProcessor&) = delete;
    D3D12PyramidProcessor(D3D12PyramidProcessor&&) noexcept;
    D3D12PyramidProcessor& operator=(D3D12PyramidProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordDownsample2x(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const PyramidDownsampleDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordDownsample2xView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const PyramidDownsampleDesc& desc,
        const D3D12ProcessingStateDesc& state);

    void RecordUpsample2x(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const PyramidUpsampleDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordUpsample2xView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const PyramidUpsampleDesc& desc,
        const D3D12ProcessingStateDesc& state);

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    D3D12Resource CreateDownsampledTexture(
        D3D12Core& core,
        UINT srcWidth,
        UINT srcHeight,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    D3D12Resource CreateUpsampledTexture(
        D3D12Core& core,
        UINT srcWidth,
        UINT srcHeight,
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
