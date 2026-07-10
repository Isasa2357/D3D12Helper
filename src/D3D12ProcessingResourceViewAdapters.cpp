//
// D3D12ProcessingResourceViewAdapters.cpp
// Non-owning Processing entry points for processors whose owned-resource paths
// are already implemented and tested.
//
#include <D3D12Helper/D3D12Processing/D3D12Resize.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Remap.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Composite.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ColorAdjust.hpp>
#include <D3D12Helper/D3D12Processing/D3D12KernelFilter.hpp>
#include <D3D12Helper/D3D12Processing/D3D12RegionEffect.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12RegionBlur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12MaskProcessor.hpp>

#include <string>

namespace D3D12CoreLib {
namespace Processing {
namespace {

// Adapts a borrowed raw resource to the existing owned-resource implementation
// without AddRef / Release. The D3D12Resource member starts empty; assigning its
// internal pointer therefore cannot release a previous object. The pointer is
// detached in this object's destructor before the ComPtr member is destroyed,
// including during exception unwinding. The adapter never escapes the View call.
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
    ScopedBorrowedResourceAdapter(ScopedBorrowedResourceAdapter&&) = delete;
    ScopedBorrowedResourceAdapter& operator=(ScopedBorrowedResourceAdapter&&) = delete;

    D3D12Resource& Resource() noexcept { return m_resource; }

private:
    D3D12Resource m_resource;
};

void RequireExplicitStates(bool useExplicitStates, const char* functionName) {
    if (!useExplicitStates) {
        throw ValidationError(
            std::string(functionName) +
            ": non-owning resource views require explicit before/after states");
    }
}

void ValidateAliasedInputStates(
    D3D12ResourceView first,
    D3D12ResourceView second,
    const D3D12ProcessingTwoInputStateDesc& state,
    const char* functionName) {

    if (first.Get() != nullptr && first.Get() == second.Get() &&
        (state.src0Before != state.src1Before ||
         state.src0After != state.src1After)) {
        throw ValidationError(
            std::string(functionName) +
            ": aliased input views must use identical before/after states");
    }
}

void RequireDistinctInputs(
    D3D12ResourceView first,
    D3D12ResourceView second,
    const char* functionName) {

    if (first.Get() != nullptr && first.Get() == second.Get()) {
        throw ValidationError(
            std::string(functionName) +
            ": aliased input views are not supported by this pass");
    }
}

void RequireDistinctInputs(
    D3D12ResourceView first,
    D3D12ResourceView second,
    D3D12ResourceView third,
    const char* functionName) {

    RequireDistinctInputs(first, second, functionName);
    RequireDistinctInputs(first, third, functionName);
    RequireDistinctInputs(second, third, functionName);
}

} // namespace

void D3D12Resizer::RecordResizeView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const ResizeDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn = "D3D12Resizer::RecordResizeView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordResize(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12Remapper::RecordRemapView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView map,
    D3D12ResourceView dst,
    const RemapDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn = "D3D12Remapper::RecordRemapView";
    RequireExplicitStates(state.useExplicitStates, fn);
    ValidateAliasedInputStates(src, map, state, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter mapAdapter(map);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordRemap(
        commandContext,
        srcAdapter.Resource(),
        mapAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12Compositor::RecordCompositeView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView base,
    D3D12ResourceView overlay,
    D3D12ResourceView dst,
    const CompositeDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn = "D3D12Compositor::RecordCompositeView";
    RequireExplicitStates(state.useExplicitStates, fn);
    ValidateAliasedInputStates(base, overlay, state, fn);

    ScopedBorrowedResourceAdapter baseAdapter(base);
    ScopedBorrowedResourceAdapter overlayAdapter(overlay);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordComposite(
        commandContext,
        baseAdapter.Resource(),
        overlayAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12ColorAdjuster::RecordColorAdjustView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const ColorAdjustDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn = "D3D12ColorAdjuster::RecordColorAdjustView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordColorAdjust(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12KernelFilter::RecordKernelFilterView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const KernelFilterDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn = "D3D12KernelFilter::RecordKernelFilterView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordKernelFilter(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12RegionEffectProcessor::RecordRegionEffectView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const RegionEffectDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn = "D3D12RegionEffectProcessor::RecordRegionEffectView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordRegionEffect(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12Blurrer::RecordBlurView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView scratch,
    D3D12ResourceView dst,
    const BlurDesc& desc,
    const D3D12ProcessingBlurStateDesc& state) {

    constexpr const char* fn = "D3D12Blurrer::RecordBlurView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter scratchAdapter(scratch);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordBlur(
        commandContext,
        srcAdapter.Resource(),
        scratchAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12RegionBlur::RecordRegionBlurView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView blurScratch,
    D3D12ResourceView blurred,
    D3D12ResourceView dst,
    const RegionBlurDesc& desc,
    const D3D12ProcessingRegionBlurStateDesc& state) {

    constexpr const char* fn = "D3D12RegionBlur::RecordRegionBlurView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter scratchAdapter(blurScratch);
    ScopedBorrowedResourceAdapter blurredAdapter(blurred);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordRegionBlur(
        commandContext,
        srcAdapter.Resource(),
        scratchAdapter.Resource(),
        blurredAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12MaskProcessor::RecordApplyMaskView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView mask,
    D3D12ResourceView dst,
    const MaskApplyDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn = "D3D12MaskProcessor::RecordApplyMaskView";
    RequireExplicitStates(state.useExplicitStates, fn);
    RequireDistinctInputs(src, mask, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter maskAdapter(mask);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordApplyMask(
        commandContext,
        srcAdapter.Resource(),
        maskAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12MaskProcessor::RecordBlendByMaskView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView base,
    D3D12ResourceView overlay,
    D3D12ResourceView mask,
    D3D12ResourceView dst,
    const MaskBlendDesc& desc,
    const D3D12ProcessingThreeInputStateDesc& state) {

    constexpr const char* fn = "D3D12MaskProcessor::RecordBlendByMaskView";
    RequireExplicitStates(state.useExplicitStates, fn);
    RequireDistinctInputs(base, overlay, mask, fn);

    ScopedBorrowedResourceAdapter baseAdapter(base);
    ScopedBorrowedResourceAdapter overlayAdapter(overlay);
    ScopedBorrowedResourceAdapter maskAdapter(mask);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordBlendByMask(
        commandContext,
        baseAdapter.Resource(),
        overlayAdapter.Resource(),
        maskAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12MaskProcessor::RecordCombineMasksView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView maskA,
    D3D12ResourceView maskB,
    D3D12ResourceView dst,
    const MaskCombineDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn = "D3D12MaskProcessor::RecordCombineMasksView";
    RequireExplicitStates(state.useExplicitStates, fn);
    RequireDistinctInputs(maskA, maskB, fn);

    ScopedBorrowedResourceAdapter maskAAdapter(maskA);
    ScopedBorrowedResourceAdapter maskBAdapter(maskB);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordCombineMasks(
        commandContext,
        maskAAdapter.Resource(),
        maskBAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12MaskProcessor::RecordInvertMaskView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView mask,
    D3D12ResourceView dst,
    const MaskInvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn = "D3D12MaskProcessor::RecordInvertMaskView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter maskAdapter(mask);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordInvertMask(
        commandContext,
        maskAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

} // namespace Processing
} // namespace D3D12CoreLib
