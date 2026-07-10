#pragma once
//
// D3D12MaskProcessor.hpp
// GPU mask application, mask blending, inversion, and mask combination passes.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12MaskProcessor {
public:
    D3D12MaskProcessor();
    ~D3D12MaskProcessor();

    D3D12MaskProcessor(const D3D12MaskProcessor&) = delete;
    D3D12MaskProcessor& operator=(const D3D12MaskProcessor&) = delete;
    D3D12MaskProcessor(D3D12MaskProcessor&&) noexcept;
    D3D12MaskProcessor& operator=(D3D12MaskProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordApplyMask(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& mask,
        D3D12Resource& dst,
        const MaskApplyDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state = {});

    void RecordApplyMaskView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView mask,
        D3D12ResourceView dst,
        const MaskApplyDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state);

    void RecordBlendByMask(
        D3D12CommandContext& commandContext,
        D3D12Resource& base,
        D3D12Resource& overlay,
        D3D12Resource& mask,
        D3D12Resource& dst,
        const MaskBlendDesc& desc,
        const D3D12ProcessingThreeInputStateDesc& state = {});

    void RecordBlendByMaskView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView base,
        D3D12ResourceView overlay,
        D3D12ResourceView mask,
        D3D12ResourceView dst,
        const MaskBlendDesc& desc,
        const D3D12ProcessingThreeInputStateDesc& state);

    void RecordCombineMasks(
        D3D12CommandContext& commandContext,
        D3D12Resource& maskA,
        D3D12Resource& maskB,
        D3D12Resource& dst,
        const MaskCombineDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state = {});

    void RecordCombineMasksView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView maskA,
        D3D12ResourceView maskB,
        D3D12ResourceView dst,
        const MaskCombineDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state);

    void RecordInvertMask(
        D3D12CommandContext& commandContext,
        D3D12Resource& mask,
        D3D12Resource& dst,
        const MaskInvertDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordInvertMaskView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView mask,
        D3D12ResourceView dst,
        const MaskInvertDesc& desc,
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
