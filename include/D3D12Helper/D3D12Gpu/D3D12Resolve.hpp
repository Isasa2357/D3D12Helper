#pragma once
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

bool IsMultisampledTexture(const D3D12Resource& texture) noexcept;

void RecordResolveSubresource(
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    UINT dstSubresource,
    D3D12Resource& srcTexture,
    UINT srcSubresource,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    D3D12_RESOURCE_STATES dstFinalState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATES srcFinalState = D3D12_RESOURCE_STATE_COMMON);

} // namespace D3D12CoreLib
