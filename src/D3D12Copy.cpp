#include <D3D12Helper/D3D12Gpu/D3D12Copy.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace {

void RequireResource(const D3D12Resource& r, const char* fn, const char* name) {
    if (!r.Get()) throw std::runtime_error(std::string(fn) + ": null " + name);
}

void RequireBuffer(const D3D12Resource& r, const char* fn, const char* name) {
    RequireResource(r, fn, name);
    if (r.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        throw std::runtime_error(std::string(fn) + ": " + name + " is not a buffer");
    }
}

void RequireTexture(const D3D12Resource& r, const char* fn, const char* name) {
    RequireResource(r, fn, name);
    if (r.GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        throw std::runtime_error(std::string(fn) + ": " + name + " is not a texture");
    }
}

void Transition(D3D12CommandContext& ctx, D3D12Resource& r, D3D12_RESOURCE_STATES after) {
    const auto before = r.GetState();
    if (before == after) return;
    ctx.ResourceBarrier(MakeTransitionBarrier(r.Get(), before, after));
    r.SetState(after);
}

void ValidateBox2D(const D3D12_RESOURCE_DESC& desc, const D3D12_BOX& box, const char* fn) {
    if (box.left >= box.right || box.top >= box.bottom) throw std::runtime_error(std::string(fn) + ": empty source box");
    if (box.front != 0 || box.back != 1) throw std::runtime_error(std::string(fn) + ": Texture2D box depth must be [0,1]");
    if (box.right > desc.Width || box.bottom > desc.Height) throw std::runtime_error(std::string(fn) + ": source box is out of bounds");
}

} // namespace

void RecordCopyResource(D3D12CommandContext& ctx, D3D12Resource& dst, D3D12Resource& src, D3D12_RESOURCE_STATES dstFinalState, D3D12_RESOURCE_STATES srcFinalState) {
    constexpr const char* fn = "RecordCopyResource";
    RequireResource(dst, fn, "dst");
    RequireResource(src, fn, "src");
    Transition(ctx, dst, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(ctx, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ctx.GetCommandList()->CopyResource(dst.Get(), src.Get());
    Transition(ctx, dst, dstFinalState);
    Transition(ctx, src, srcFinalState);
}

void RecordCopyBufferRegion(D3D12CommandContext& ctx, D3D12Resource& dstBuffer, UINT64 dstOffset, D3D12Resource& srcBuffer, UINT64 srcOffset, UINT64 byteCount, D3D12_RESOURCE_STATES dstFinalState, D3D12_RESOURCE_STATES srcFinalState) {
    constexpr const char* fn = "RecordCopyBufferRegion";
    RequireBuffer(dstBuffer, fn, "dstBuffer");
    RequireBuffer(srcBuffer, fn, "srcBuffer");
    if (byteCount == 0) throw std::runtime_error(std::string(fn) + ": byteCount must be > 0");
    if (dstOffset + byteCount > dstBuffer.GetDesc().Width) throw std::runtime_error(std::string(fn) + ": destination range is out of bounds");
    if (srcOffset + byteCount > srcBuffer.GetDesc().Width) throw std::runtime_error(std::string(fn) + ": source range is out of bounds");
    Transition(ctx, dstBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(ctx, srcBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ctx.GetCommandList()->CopyBufferRegion(dstBuffer.Get(), dstOffset, srcBuffer.Get(), srcOffset, byteCount);
    Transition(ctx, dstBuffer, dstFinalState);
    Transition(ctx, srcBuffer, srcFinalState);
}

void RecordCopyTextureSubresource(D3D12CommandContext& ctx, D3D12Resource& dstTexture, UINT dstSubresource, D3D12Resource& srcTexture, UINT srcSubresource, D3D12_RESOURCE_STATES dstFinalState, D3D12_RESOURCE_STATES srcFinalState) {
    constexpr const char* fn = "RecordCopyTextureSubresource";
    RequireTexture(dstTexture, fn, "dstTexture");
    RequireTexture(srcTexture, fn, "srcTexture");
    Transition(ctx, dstTexture, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(ctx, srcTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = dstTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = dstSubresource;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = srcTexture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = srcSubresource;
    ctx.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(ctx, dstTexture, dstFinalState);
    Transition(ctx, srcTexture, srcFinalState);
}

void RecordCopyTextureRegion(D3D12CommandContext& ctx, D3D12Resource& dstTexture, UINT dstSubresource, UINT dstX, UINT dstY, D3D12Resource& srcTexture, UINT srcSubresource, const D3D12_BOX& srcBox, D3D12_RESOURCE_STATES dstFinalState, D3D12_RESOURCE_STATES srcFinalState) {
    constexpr const char* fn = "RecordCopyTextureRegion";
    RequireTexture(dstTexture, fn, "dstTexture");
    RequireTexture(srcTexture, fn, "srcTexture");
    ValidateBox2D(srcTexture.GetDesc(), srcBox, fn);
    Transition(ctx, dstTexture, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(ctx, srcTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = dstTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = dstSubresource;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = srcTexture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = srcSubresource;
    ctx.GetCommandList()->CopyTextureRegion(&dst, dstX, dstY, 0, &src, &srcBox);
    Transition(ctx, dstTexture, dstFinalState);
    Transition(ctx, srcTexture, srcFinalState);
}

} // namespace D3D12CoreLib
