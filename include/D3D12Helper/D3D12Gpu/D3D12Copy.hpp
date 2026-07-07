#pragma once
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

void RecordCopyResource(
    D3D12CommandContext& ctx,
    D3D12Resource& dst,
    D3D12Resource& src,
    D3D12_RESOURCE_STATES dstFinalState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATES srcFinalState = D3D12_RESOURCE_STATE_COMMON);

void RecordCopyBufferRegion(
    D3D12CommandContext& ctx,
    D3D12Resource& dstBuffer,
    UINT64 dstOffset,
    D3D12Resource& srcBuffer,
    UINT64 srcOffset,
    UINT64 byteCount,
    D3D12_RESOURCE_STATES dstFinalState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATES srcFinalState = D3D12_RESOURCE_STATE_COMMON);

void RecordCopyTextureSubresource(
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    UINT dstSubresource,
    D3D12Resource& srcTexture,
    UINT srcSubresource,
    D3D12_RESOURCE_STATES dstFinalState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATES srcFinalState = D3D12_RESOURCE_STATE_COMMON);

void RecordCopyTextureRegion(
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    UINT dstSubresource,
    UINT dstX,
    UINT dstY,
    D3D12Resource& srcTexture,
    UINT srcSubresource,
    const D3D12_BOX& srcBox,
    D3D12_RESOURCE_STATES dstFinalState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_STATES srcFinalState = D3D12_RESOURCE_STATE_COMMON);

} // namespace D3D12CoreLib
