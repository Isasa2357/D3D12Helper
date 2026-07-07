#include <D3D12Helper/D3D12Gpu/D3D12Resolve.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace {

void RequireTexture(const D3D12Resource& r, const char* fn, const char* name) {
    if (!r.Get()) throw std::runtime_error(std::string(fn) + ": null " + name);
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

} // namespace

bool IsMultisampledTexture(const D3D12Resource& texture) noexcept {
    return texture.Get() && texture.GetDesc().SampleDesc.Count > 1;
}

void RecordResolveSubresource(D3D12CommandContext& ctx, D3D12Resource& dstTexture, UINT dstSubresource, D3D12Resource& srcTexture, UINT srcSubresource, DXGI_FORMAT format, D3D12_RESOURCE_STATES dstFinalState, D3D12_RESOURCE_STATES srcFinalState) {
    constexpr const char* fn = "RecordResolveSubresource";
    RequireTexture(dstTexture, fn, "dstTexture");
    RequireTexture(srcTexture, fn, "srcTexture");
    const auto dstDesc = dstTexture.GetDesc();
    const auto srcDesc = srcTexture.GetDesc();
    if (srcDesc.SampleDesc.Count <= 1) throw std::runtime_error(std::string(fn) + ": source texture must be multisampled");
    if (dstDesc.SampleDesc.Count != 1) throw std::runtime_error(std::string(fn) + ": destination texture must be single-sampled");
    if (dstDesc.Dimension != srcDesc.Dimension) throw std::runtime_error(std::string(fn) + ": texture dimensions do not match");
    if (dstDesc.Width != srcDesc.Width || dstDesc.Height != srcDesc.Height) throw std::runtime_error(std::string(fn) + ": texture size does not match");
    const DXGI_FORMAT resolvedFormat = (format == DXGI_FORMAT_UNKNOWN) ? dstDesc.Format : format;
    if (resolvedFormat == DXGI_FORMAT_UNKNOWN) throw std::runtime_error(std::string(fn) + ": resolve format must not be UNKNOWN");
    Transition(ctx, dstTexture, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    Transition(ctx, srcTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    ctx.GetCommandList()->ResolveSubresource(dstTexture.Get(), dstSubresource, srcTexture.Get(), srcSubresource, resolvedFormat);
    Transition(ctx, dstTexture, dstFinalState);
    Transition(ctx, srcTexture, srcFinalState);
}

} // namespace D3D12CoreLib
