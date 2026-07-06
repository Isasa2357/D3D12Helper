#pragma once
//
// D3D12ColorAdjust.hpp
// Single-pass GPU color adjustment for RGBA-like textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12ColorAdjuster {
public:
    D3D12ColorAdjuster();
    ~D3D12ColorAdjuster();

    D3D12ColorAdjuster(const D3D12ColorAdjuster&) = delete;
    D3D12ColorAdjuster& operator=(const D3D12ColorAdjuster&) = delete;
    D3D12ColorAdjuster(D3D12ColorAdjuster&&) noexcept;
    D3D12ColorAdjuster& operator=(D3D12ColorAdjuster&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordColorAdjust(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const ColorAdjustDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

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
