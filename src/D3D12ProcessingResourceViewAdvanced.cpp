//
// D3D12ProcessingResourceViewAdvanced.cpp
// Non-owning AdvancedProcessing entry points with mandatory explicit state.
//
#include <D3D12Helper/D3D12Processing/D3D12AdvancedProcessing.hpp>

#include <string>

namespace D3D12CoreLib {
namespace Processing {
namespace {

// The D3D12Resource member begins empty. Assigning the borrowed pointer directly
// to its ComPtr storage therefore performs no AddRef. The pointer is detached
// before destruction, including during exception unwinding, so no Release occurs.
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

} // namespace

void D3D12AdvancedProcessor::RecordAffineTransformView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const AffineTransformDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn =
        "D3D12AdvancedProcessor::RecordAffineTransformView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordAffineTransform(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12AdvancedProcessor::RecordPerspectiveTransformView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView dst,
    const PerspectiveTransformDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    constexpr const char* fn =
        "D3D12AdvancedProcessor::RecordPerspectiveTransformView";
    RequireExplicitStates(state.useExplicitStates, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordPerspectiveTransform(
        commandContext,
        srcAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12AdvancedProcessor::RecordApplyLut3DView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView lut,
    D3D12ResourceView dst,
    const Lut3DDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn =
        "D3D12AdvancedProcessor::RecordApplyLut3DView";
    RequireExplicitStates(state.useExplicitStates, fn);
    ValidateAliasedInputStates(src, lut, state, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter lutAdapter(lut);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordApplyLut3D(
        commandContext,
        srcAdapter.Resource(),
        lutAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

void D3D12AdvancedProcessor::RecordApplyUndistortMapView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    D3D12ResourceView map,
    D3D12ResourceView dst,
    const RemapDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr const char* fn =
        "D3D12AdvancedProcessor::RecordApplyUndistortMapView";
    RequireExplicitStates(state.useExplicitStates, fn);
    ValidateAliasedInputStates(src, map, state, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src);
    ScopedBorrowedResourceAdapter mapAdapter(map);
    ScopedBorrowedResourceAdapter dstAdapter(dst);
    RecordApplyUndistortMap(
        commandContext,
        srcAdapter.Resource(),
        mapAdapter.Resource(),
        dstAdapter.Resource(),
        desc,
        state);
}

} // namespace Processing
} // namespace D3D12CoreLib
