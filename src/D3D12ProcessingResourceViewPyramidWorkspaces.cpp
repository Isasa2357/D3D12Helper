//
// D3D12ProcessingResourceViewPyramidWorkspaces.cpp
//
#include <D3D12Helper/D3D12Processing/D3D12PyramidBlur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidRegionBlur.hpp>
#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace D3D12CoreLib {
namespace Processing {
namespace {

void AttachBorrowed(
    D3D12Resource& resource,
    D3D12ResourceView view,
    D3D12_RESOURCE_STATES state) noexcept {

    *resource.GetAddressOf() = view.Get();
    resource.SetState(state);
}

void DetachBorrowed(D3D12Resource& resource) noexcept {
    *resource.GetAddressOf() = nullptr;
}

class ScopedBorrowedResourceAdapter {
public:
    ScopedBorrowedResourceAdapter(
        D3D12ResourceView view,
        D3D12_RESOURCE_STATES state) noexcept {
        AttachBorrowed(m_resource, view, state);
    }

    ~ScopedBorrowedResourceAdapter() {
        DetachBorrowed(m_resource);
    }

    ScopedBorrowedResourceAdapter(const ScopedBorrowedResourceAdapter&) = delete;
    ScopedBorrowedResourceAdapter& operator=(const ScopedBorrowedResourceAdapter&) = delete;

    D3D12Resource& Resource() noexcept { return m_resource; }

private:
    D3D12Resource m_resource;
};

void ValidateWorkspaceStateCounts(
    const D3D12PyramidBlurWorkspaceView& workspace,
    const D3D12PyramidBlurWorkspaceStateDesc& state,
    const char* functionName) {

    if (state.downBefore.size() != workspace.downTextures.size() ||
        state.downAfter.size() != workspace.downTextures.size()) {
        throw ValidationError(
            std::string(functionName) +
            ": down state counts must match downTextures");
    }

    if (state.upBefore.size() != workspace.upTextures.size() ||
        state.upAfter.size() != workspace.upTextures.size()) {
        throw ValidationError(
            std::string(functionName) +
            ": up state counts must match upTextures");
    }
}

void AppendWorkspaceViews(
    std::vector<ID3D12Resource*>& resources,
    const D3D12PyramidBlurWorkspaceView& workspace) {

    for (const auto& view : workspace.downTextures) {
        resources.push_back(view.Get());
    }
    resources.push_back(workspace.blurScratch.Get());
    resources.push_back(workspace.blurredLow.Get());
    for (const auto& view : workspace.upTextures) {
        resources.push_back(view.Get());
    }
}

void ValidateDistinctNonNull(
    const std::vector<ID3D12Resource*>& resources,
    const char* functionName) {

    std::vector<ID3D12Resource*> seen;
    seen.reserve(resources.size());

    for (auto* resource : resources) {
        if (!resource) {
            throw ValidationError(
                std::string(functionName) + ": resource view is null");
        }
        if (std::find(seen.begin(), seen.end(), resource) != seen.end()) {
            throw ValidationError(
                std::string(functionName) +
                ": source, destination, and workspace resources must be distinct");
        }
        seen.push_back(resource);
    }
}

void PrepareBorrowedWorkspace(
    D3D12PyramidBlurWorkspace& owned,
    const D3D12PyramidBlurWorkspaceView& view,
    const D3D12PyramidBlurWorkspaceStateDesc& state) {

    owned.sourceWidth = view.sourceWidth;
    owned.sourceHeight = view.sourceHeight;
    owned.levels = view.levels;
    owned.format = view.format;

    // Allocate all vector storage before attaching any borrowed pointers. This
    // keeps construction exception-safe: no partially attached ComPtr can be
    // destroyed by vector allocation failure.
    owned.downTextures.resize(view.downTextures.size());
    owned.upTextures.resize(view.upTextures.size());

    for (size_t i = 0; i < view.downTextures.size(); ++i) {
        AttachBorrowed(owned.downTextures[i], view.downTextures[i], state.downBefore[i]);
    }
    AttachBorrowed(owned.blurScratch, view.blurScratch, state.blurScratchBefore);
    AttachBorrowed(owned.blurredLow, view.blurredLow, state.blurredLowBefore);
    for (size_t i = 0; i < view.upTextures.size(); ++i) {
        AttachBorrowed(owned.upTextures[i], view.upTextures[i], state.upBefore[i]);
    }
}

void DetachBorrowedWorkspace(D3D12PyramidBlurWorkspace& workspace) noexcept {
    for (auto& resource : workspace.downTextures) {
        DetachBorrowed(resource);
    }
    DetachBorrowed(workspace.blurScratch);
    DetachBorrowed(workspace.blurredLow);
    for (auto& resource : workspace.upTextures) {
        DetachBorrowed(resource);
    }
}

class ScopedBorrowedPyramidBlurWorkspaceAdapter {
public:
    ScopedBorrowedPyramidBlurWorkspaceAdapter(
        const D3D12PyramidBlurWorkspaceView& view,
        const D3D12PyramidBlurWorkspaceStateDesc& state) {
        PrepareBorrowedWorkspace(m_workspace, view, state);
    }

    ~ScopedBorrowedPyramidBlurWorkspaceAdapter() {
        DetachBorrowedWorkspace(m_workspace);
    }

    ScopedBorrowedPyramidBlurWorkspaceAdapter(
        const ScopedBorrowedPyramidBlurWorkspaceAdapter&) = delete;
    ScopedBorrowedPyramidBlurWorkspaceAdapter& operator=(
        const ScopedBorrowedPyramidBlurWorkspaceAdapter&) = delete;

    D3D12PyramidBlurWorkspace& Workspace() noexcept { return m_workspace; }

private:
    D3D12PyramidBlurWorkspace m_workspace;
};

class ScopedBorrowedPyramidRegionBlurWorkspaceAdapter {
public:
    ScopedBorrowedPyramidRegionBlurWorkspaceAdapter(
        const D3D12PyramidRegionBlurWorkspaceView& view,
        const D3D12PyramidRegionBlurStateDesc& state) {

        m_workspace.sourceWidth = view.sourceWidth;
        m_workspace.sourceHeight = view.sourceHeight;
        m_workspace.levels = view.levels;
        m_workspace.format = view.format;

        PrepareBorrowedWorkspace(
            m_workspace.blurWorkspace,
            view.blurWorkspace,
            state.blurWorkspace);
        AttachBorrowed(
            m_workspace.blurred,
            view.blurred,
            state.blurredBefore);
    }

    ~ScopedBorrowedPyramidRegionBlurWorkspaceAdapter() {
        DetachBorrowed(m_workspace.blurred);
        DetachBorrowedWorkspace(m_workspace.blurWorkspace);
    }

    ScopedBorrowedPyramidRegionBlurWorkspaceAdapter(
        const ScopedBorrowedPyramidRegionBlurWorkspaceAdapter&) = delete;
    ScopedBorrowedPyramidRegionBlurWorkspaceAdapter& operator=(
        const ScopedBorrowedPyramidRegionBlurWorkspaceAdapter&) = delete;

    D3D12PyramidRegionBlurWorkspace& Workspace() noexcept { return m_workspace; }

private:
    D3D12PyramidRegionBlurWorkspace m_workspace;
};

void AddFinalTransition(
    D3D12BarrierBatch& batch,
    D3D12Resource& resource,
    D3D12_RESOURCE_STATES finalState) {

    batch.Transition(resource.Get(), resource.GetState(), finalState);
}

void AddWorkspaceFinalTransitions(
    D3D12BarrierBatch& batch,
    D3D12PyramidBlurWorkspace& workspace,
    const D3D12PyramidBlurWorkspaceStateDesc& state) {

    for (size_t i = 0; i < workspace.downTextures.size(); ++i) {
        AddFinalTransition(batch, workspace.downTextures[i], state.downAfter[i]);
    }
    AddFinalTransition(batch, workspace.blurScratch, state.blurScratchAfter);
    AddFinalTransition(batch, workspace.blurredLow, state.blurredLowAfter);
    for (size_t i = 0; i < workspace.upTextures.size(); ++i) {
        AddFinalTransition(batch, workspace.upTextures[i], state.upAfter[i]);
    }
}

void SubmitFinalTransitions(
    D3D12CommandContext& commandContext,
    const D3D12BarrierBatch& batch) {

    if (!batch.Empty()) {
        commandContext.ResourceBarrier(batch.Count(), batch.Data());
    }
}

} // namespace

void D3D12PyramidBlur::RecordPyramidBlurView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    const D3D12PyramidBlurWorkspaceView& workspace,
    D3D12ResourceView dst,
    const PyramidBlurDesc& desc,
    const D3D12PyramidBlurStateDesc& state) {

    constexpr const char* fn = "D3D12PyramidBlur::RecordPyramidBlurView";
    if (!state.useExplicitStates) {
        throw ValidationError(
            std::string(fn) +
            ": non-owning resource views require explicit before/after states");
    }

    ValidateWorkspaceStateCounts(workspace, state.workspace, fn);

    std::vector<ID3D12Resource*> resources;
    resources.reserve(4u + workspace.downTextures.size() + workspace.upTextures.size());
    resources.push_back(src.Get());
    resources.push_back(dst.Get());
    AppendWorkspaceViews(resources, workspace);
    ValidateDistinctNonNull(resources, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src, state.srcBefore);
    ScopedBorrowedResourceAdapter dstAdapter(dst, state.dstBefore);
    ScopedBorrowedPyramidBlurWorkspaceAdapter workspaceAdapter(
        workspace,
        state.workspace);

    RecordPyramidBlur(
        commandContext,
        srcAdapter.Resource(),
        workspaceAdapter.Workspace(),
        dstAdapter.Resource(),
        desc);

    D3D12BarrierBatch finalBarriers;
    finalBarriers.Reserve(resources.size());
    AddFinalTransition(finalBarriers, srcAdapter.Resource(), state.srcAfter);
    AddFinalTransition(finalBarriers, dstAdapter.Resource(), state.dstAfter);
    AddWorkspaceFinalTransitions(
        finalBarriers,
        workspaceAdapter.Workspace(),
        state.workspace);
    SubmitFinalTransitions(commandContext, finalBarriers);
}

void D3D12PyramidRegionBlur::RecordPyramidRegionBlurView(
    D3D12CommandContext& commandContext,
    D3D12ResourceView src,
    const D3D12PyramidRegionBlurWorkspaceView& workspace,
    D3D12ResourceView dst,
    const PyramidRegionBlurDesc& desc,
    const D3D12PyramidRegionBlurStateDesc& state) {

    constexpr const char* fn =
        "D3D12PyramidRegionBlur::RecordPyramidRegionBlurView";
    if (!state.useExplicitStates) {
        throw ValidationError(
            std::string(fn) +
            ": non-owning resource views require explicit before/after states");
    }

    ValidateWorkspaceStateCounts(
        workspace.blurWorkspace,
        state.blurWorkspace,
        fn);

    std::vector<ID3D12Resource*> resources;
    resources.reserve(
        5u +
        workspace.blurWorkspace.downTextures.size() +
        workspace.blurWorkspace.upTextures.size());
    resources.push_back(src.Get());
    resources.push_back(dst.Get());
    AppendWorkspaceViews(resources, workspace.blurWorkspace);
    resources.push_back(workspace.blurred.Get());
    ValidateDistinctNonNull(resources, fn);

    ScopedBorrowedResourceAdapter srcAdapter(src, state.srcBefore);
    ScopedBorrowedResourceAdapter dstAdapter(dst, state.dstBefore);
    ScopedBorrowedPyramidRegionBlurWorkspaceAdapter workspaceAdapter(
        workspace,
        state);

    RecordPyramidRegionBlur(
        commandContext,
        srcAdapter.Resource(),
        workspaceAdapter.Workspace(),
        dstAdapter.Resource(),
        desc);

    D3D12BarrierBatch finalBarriers;
    finalBarriers.Reserve(resources.size());
    AddFinalTransition(finalBarriers, srcAdapter.Resource(), state.srcAfter);
    AddFinalTransition(finalBarriers, dstAdapter.Resource(), state.dstAfter);
    AddWorkspaceFinalTransitions(
        finalBarriers,
        workspaceAdapter.Workspace().blurWorkspace,
        state.blurWorkspace);
    AddFinalTransition(
        finalBarriers,
        workspaceAdapter.Workspace().blurred,
        state.blurredAfter);
    SubmitFinalTransitions(commandContext, finalBarriers);
}

} // namespace Processing
} // namespace D3D12CoreLib
