//
// D3D12ProcessingResourceViewAdapters.cpp
// Non-owning Processing entry points for processors whose owned-resource paths
// are already implemented and tested.
//
#include <D3D12Helper/D3D12Processing/D3D12Resize.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Remap.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Composite.hpp>

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

} // namespace Processing
} // namespace D3D12CoreLib
