//
// D3D12ProcessingResourceViewThresholdPyramid.cpp
//
#include <D3D12Helper/D3D12Processing/D3D12ThresholdProcessor.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidProcessor.hpp>

#include <string>

namespace D3D12CoreLib {
namespace Processing {
namespace {

class ScopedBorrowedResourceAdapter {
public:
    explicit ScopedBorrowedResourceAdapter(D3D12ResourceView view) noexcept {
        *m_resource.GetAddressOf() = view.Get();
    }

    ~ScopedBorrowedResourceAdapter() {
        *m_resource.GetAddressOf() = nullptr;
    }

    ScopedBorrowedResourceAdapter(const ScopedBorrowedResourceAdapter&) = delete;
    ScopedBorrowedResourceAdapter& operator=(const ScopedBorrowedResourceAdapter&) = delete;

    D3D12Resource& Resource() noexcept { return m_resource; }

private:
    D3D12Resource m_resource;
};

void RequireExplicitStates(
    const D3D12ProcessingStateDesc& state,
    const char* functionName) {

    if (!state.useExplicitStates) {
        throw ValidationError(
            std::string(functionName) +
            ": non-owning resource views require explicit before/after states");
    }
}

template <class Desc, class Callable>
void RecordOneInputView(
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const Desc& desc,
    const D3D12ProcessingStateDesc& state,
    const char* functionName,
    Callable&& callable) {

    RequireExplicitStates(state, functionName);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    callable(srcAdapter.Resource(), dstAdapter.Resource(), desc, state);
}

} // namespace

void D3D12ThresholdProcessor::RecordThresholdView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const ThresholdDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12ThresholdProcessor::RecordThresholdView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const ThresholdDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordThreshold(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12ThresholdProcessor::RecordRangeThresholdView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const RangeThresholdDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12ThresholdProcessor::RecordRangeThresholdView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const RangeThresholdDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordRangeThreshold(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12ThresholdProcessor::RecordConfidenceHeatmapView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const ConfidenceHeatmapDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12ThresholdProcessor::RecordConfidenceHeatmapView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const ConfidenceHeatmapDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordConfidenceHeatmap(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12ThresholdProcessor::RecordClassColorMapView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const ClassColorMapDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12ThresholdProcessor::RecordClassColorMapView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const ClassColorMapDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordClassColorMap(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12ThresholdProcessor::RecordMaskOverlayView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView mask,
    D3D12ResourceView dst,
    const MaskOverlayDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        mask, dst, desc, state,
        "D3D12ThresholdProcessor::RecordMaskOverlayView",
        [&](D3D12Resource& ownedMask, D3D12Resource& ownedDst,
            const MaskOverlayDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordMaskOverlay(commandContext, ownedMask, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12PyramidProcessor::RecordDownsample2xView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const PyramidDownsampleDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12PyramidProcessor::RecordDownsample2xView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const PyramidDownsampleDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordDownsample2x(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

void D3D12PyramidProcessor::RecordUpsample2xView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const PyramidUpsampleDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    RecordOneInputView(
        src, dst, desc, state,
        "D3D12PyramidProcessor::RecordUpsample2xView",
        [&](D3D12Resource& ownedSrc, D3D12Resource& ownedDst,
            const PyramidUpsampleDesc& ownedDesc,
            const D3D12ProcessingStateDesc& ownedState) {
            RecordUpsample2x(commandContext, ownedSrc, ownedDst, ownedDesc, ownedState);
        });
}

} // namespace Processing
} // namespace D3D12CoreLib
