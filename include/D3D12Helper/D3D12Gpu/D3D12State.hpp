#pragma once
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <vector>

namespace D3D12CoreLib {

struct D3D12StateTransition {
    D3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
};

bool HasResourceState(D3D12_RESOURCE_STATES state, D3D12_RESOURCE_STATES flag) noexcept;
bool IsReadOnlyResourceState(D3D12_RESOURCE_STATES state) noexcept;
bool IsWriteResourceState(D3D12_RESOURCE_STATES state) noexcept;
bool CanImplicitlyPromoteTo(D3D12_RESOURCE_STATES state) noexcept;
const char* ResourceStateName(D3D12_RESOURCE_STATES state) noexcept;

D3D12_RESOURCE_BARRIER MakeTrackedTransitionBarrier(
    D3D12Resource& resource,
    D3D12_RESOURCE_STATES after,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE);

bool RecordTransition(
    D3D12CommandContext& ctx,
    D3D12Resource& resource,
    D3D12_RESOURCE_STATES after,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
    D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE);

UINT RecordTransitions(
    D3D12CommandContext& ctx,
    const D3D12StateTransition* transitions,
    UINT transitionCount);

UINT RecordTransitions(
    D3D12CommandContext& ctx,
    const std::vector<D3D12StateTransition>& transitions);

} // namespace D3D12CoreLib
