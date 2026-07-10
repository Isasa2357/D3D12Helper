#pragma once
//
// D3D12Blur.hpp
// Separable GPU blur pass for RGBA-like D3D12 resources.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12Blurrer {
public:
    static constexpr UINT MaxRadius = 16;

    D3D12Blurrer();
    ~D3D12Blurrer();

    D3D12Blurrer(const D3D12Blurrer&) = delete;
    D3D12Blurrer& operator=(const D3D12Blurrer&) = delete;
    D3D12Blurrer(D3D12Blurrer&&) noexcept;
    D3D12Blurrer& operator=(D3D12Blurrer&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordBlur(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& scratch,
        D3D12Resource& dst,
        const BlurDesc& desc,
        const D3D12ProcessingBlurStateDesc& state = {});

    // Non-owning entry point. All resources must outlive submitted GPU work.
    // Explicit before/after states are mandatory and all three resources must be distinct.
    void RecordBlurView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView scratch,
        D3D12ResourceView dst,
        const BlurDesc& desc,
        const D3D12ProcessingBlurStateDesc& state);

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
