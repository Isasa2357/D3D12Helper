#include <D3D12Helper/D3D12Gpu/D3D12State.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace D3D12CoreLib {

bool HasResourceState(D3D12_RESOURCE_STATES state, D3D12_RESOURCE_STATES flag) noexcept {
    return (state & flag) == flag;
}

bool IsWriteResourceState(D3D12_RESOURCE_STATES state) noexcept {
    constexpr D3D12_RESOURCE_STATES writeStates =
        D3D12_RESOURCE_STATE_RENDER_TARGET |
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
        D3D12_RESOURCE_STATE_DEPTH_WRITE |
        D3D12_RESOURCE_STATE_STREAM_OUT |
        D3D12_RESOURCE_STATE_COPY_DEST |
        D3D12_RESOURCE_STATE_RESOLVE_DEST;
    return (state & writeStates) != 0;
}

bool IsReadOnlyResourceState(D3D12_RESOURCE_STATES state) noexcept {
    if (state == D3D12_RESOURCE_STATE_COMMON) return true;
    return !IsWriteResourceState(state);
}

bool CanImplicitlyPromoteTo(D3D12_RESOURCE_STATES state) noexcept {
    if (state == D3D12_RESOURCE_STATE_COMMON) return true;
    if (IsWriteResourceState(state)) {
        return state == D3D12_RESOURCE_STATE_COPY_DEST || state == D3D12_RESOURCE_STATE_RENDER_TARGET || state == D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    return true;
}

const char* ResourceStateName(D3D12_RESOURCE_STATES state) noexcept {
    switch (state) {
    case D3D12_RESOURCE_STATE_COMMON: return "COMMON";
    case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER: return "VERTEX_AND_CONSTANT_BUFFER";
    case D3D12_RESOURCE_STATE_INDEX_BUFFER: return "INDEX_BUFFER";
    case D3D12_RESOURCE_STATE_RENDER_TARGET: return "RENDER_TARGET";
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return "UNORDERED_ACCESS";
    case D3D12_RESOURCE_STATE_DEPTH_WRITE: return "DEPTH_WRITE";
    case D3D12_RESOURCE_STATE_DEPTH_READ: return "DEPTH_READ";
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return "NON_PIXEL_SHADER_RESOURCE";
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return "PIXEL_SHADER_RESOURCE";
    case D3D12_RESOURCE_STATE_STREAM_OUT: return "STREAM_OUT";
    case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT: return "INDIRECT_ARGUMENT";
    case D3D12_RESOURCE_STATE_COPY_DEST: return "COPY_DEST";
    case D3D12_RESOURCE_STATE_COPY_SOURCE: return "COPY_SOURCE";
    case D3D12_RESOURCE_STATE_RESOLVE_DEST: return "RESOLVE_DEST";
    case D3D12_RESOURCE_STATE_RESOLVE_SOURCE: return "RESOLVE_SOURCE";
    case D3D12_RESOURCE_STATE_PRESENT: return "PRESENT";
    default: return "COMBINED_OR_UNKNOWN";
    }
}

D3D12_RESOURCE_BARRIER MakeTrackedTransitionBarrier(D3D12Resource& resource, D3D12_RESOURCE_STATES after, UINT subresource, D3D12_RESOURCE_BARRIER_FLAGS flags) {
    if (!resource.Get()) throw std::runtime_error("MakeTrackedTransitionBarrier: null resource");
    const auto before = resource.GetState();
    auto barrier = MakeTransitionBarrier(resource.Get(), before, after, subresource);
    barrier.Flags = flags;
    return barrier;
}

bool RecordTransition(D3D12CommandContext& ctx, D3D12Resource& resource, D3D12_RESOURCE_STATES after, UINT subresource, D3D12_RESOURCE_BARRIER_FLAGS flags) {
    if (!resource.Get()) throw std::runtime_error("RecordTransition: null resource");
    const auto before = resource.GetState();
    if (before == after && flags == D3D12_RESOURCE_BARRIER_FLAG_NONE) return false;
    auto barrier = MakeTransitionBarrier(resource.Get(), before, after, subresource);
    barrier.Flags = flags;
    ctx.ResourceBarrier(barrier);
    resource.SetState(after);
    return true;
}

UINT RecordTransitions(D3D12CommandContext& ctx, const D3D12StateTransition* transitions, UINT transitionCount) {
    if (transitionCount == 0) return 0;
    if (!transitions) throw std::runtime_error("RecordTransitions: null transitions");
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(transitionCount);
    std::vector<D3D12StateTransition> applied;
    applied.reserve(transitionCount);
    for (UINT i = 0; i < transitionCount; ++i) {
        const auto& t = transitions[i];
        if (!t.resource || !t.resource->Get()) throw std::runtime_error("RecordTransitions: null resource");
        const auto before = t.resource->GetState();
        if (before == t.after && t.flags == D3D12_RESOURCE_BARRIER_FLAG_NONE) continue;
        auto barrier = MakeTransitionBarrier(t.resource->Get(), before, t.after, t.subresource);
        barrier.Flags = t.flags;
        barriers.push_back(barrier);
        applied.push_back(t);
    }
    if (barriers.empty()) return 0;
    ctx.GetCommandList()->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    for (const auto& t : applied) t.resource->SetState(t.after);
    return static_cast<UINT>(barriers.size());
}

UINT RecordTransitions(D3D12CommandContext& ctx, const std::vector<D3D12StateTransition>& transitions) {
    return RecordTransitions(ctx, transitions.data(), static_cast<UINT>(transitions.size()));
}

} // namespace D3D12CoreLib
