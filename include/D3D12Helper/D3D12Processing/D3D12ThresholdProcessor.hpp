#pragma once
//
// D3D12ThresholdProcessor.hpp
// Thresholding and visualization passes for RGBA-like textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12ThresholdProcessor {
public:
    D3D12ThresholdProcessor();
    ~D3D12ThresholdProcessor();

    D3D12ThresholdProcessor(const D3D12ThresholdProcessor&) = delete;
    D3D12ThresholdProcessor& operator=(const D3D12ThresholdProcessor&) = delete;
    D3D12ThresholdProcessor(D3D12ThresholdProcessor&&) noexcept;
    D3D12ThresholdProcessor& operator=(D3D12ThresholdProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordThreshold(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const ThresholdDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordThresholdView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const ThresholdDesc& desc,
        const D3D12ProcessingStateDesc& state);

    void RecordRangeThreshold(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const RangeThresholdDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordRangeThresholdView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const RangeThresholdDesc& desc,
        const D3D12ProcessingStateDesc& state);

    void RecordConfidenceHeatmap(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const ConfidenceHeatmapDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordConfidenceHeatmapView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const ConfidenceHeatmapDesc& desc,
        const D3D12ProcessingStateDesc& state);

    void RecordClassColorMap(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const ClassColorMapDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordClassColorMapView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const ClassColorMapDesc& desc,
        const D3D12ProcessingStateDesc& state);

    void RecordMaskOverlay(
        D3D12CommandContext& commandContext,
        D3D12Resource& mask,
        D3D12Resource& dst,
        const MaskOverlayDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordMaskOverlayView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView mask,
        D3D12ResourceView dst,
        const MaskOverlayDesc& desc,
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
