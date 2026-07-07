#include <D3D12Helper/D3D12Gpu/D3D12TextureTransfer.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ReadbackBuffer.hpp>
#include <D3D12Helper/D3D12Framework/D3D12UploadBuffer.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace {

void RequireTexture2D(const D3D12Resource& tex, const char* fn) {
    if (!tex.Get()) throw std::runtime_error(std::string(fn) + ": null texture");
    const auto d = tex.GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) throw std::runtime_error(std::string(fn) + ": resource is not Texture2D");
    if (d.SampleDesc.Count != 1) throw std::runtime_error(std::string(fn) + ": MSAA texture is not supported");
    if (d.MipLevels != 1 || d.DepthOrArraySize != 1) throw std::runtime_error(std::string(fn) + ": only mip0 array0 is supported");
    if (!IsSinglePlaneCpuImageFormat(d.Format)) throw std::runtime_error(std::string(fn) + ": unsupported format");
}

void CheckBox(const D3D12_RESOURCE_DESC& d, const D3D12_BOX& b, const char* fn) {
    if (b.left >= b.right || b.top >= b.bottom) throw std::runtime_error(std::string(fn) + ": empty box");
    if (b.front != 0 || b.back != 1) throw std::runtime_error(std::string(fn) + ": Texture2D box depth must be [0,1]");
    if (b.right > d.Width || b.bottom > d.Height) throw std::runtime_error(std::string(fn) + ": box is out of bounds");
}

D3D12_RESOURCE_DESC FootprintDesc(UINT w, UINT h, DXGI_FORMAT f) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return d;
}

struct Fp {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 total = 0;
};

Fp QueryFp(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT f) {
    Fp fp;
    const auto d = FootprintDesc(w, h, f);
    dev->GetCopyableFootprints(&d, 0, 1, 0, &fp.layout, &fp.rows, &fp.rowSize, &fp.total);
    if (fp.rowSize > 0xffffffffull) throw std::runtime_error("D3D12TextureTransfer: row size too large");
    return fp;
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& ctx) {
    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitIdle();
}

} // namespace

D3D12CpuImage ReadbackTexture2DRegionToCpuImage(D3D12Core& core, const D3D12Resource& srcTexture, const D3D12_BOX& box) {
    constexpr const char* fn = "ReadbackTexture2DRegionToCpuImage";
    RequireTexture2D(srcTexture, fn);
    const auto desc = srcTexture.GetDesc();
    CheckBox(desc, box, fn);

    const UINT w = box.right - box.left;
    const UINT h = box.bottom - box.top;
    const Fp fp = QueryFp(core.GetDevice(), w, h, desc.Format);

    D3D12ReadbackBuffer rb;
    rb.Initialize(core.GetDevice(), fp.total);

    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    const auto original = srcTexture.GetState();
    if (original != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        ctx.ResourceBarrier(MakeTransitionBarrier(srcTexture.Get(), original, D3D12_RESOURCE_STATE_COPY_SOURCE));
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = rb.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp.layout;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = srcTexture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    ctx.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

    if (original != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        ctx.ResourceBarrier(MakeTransitionBarrier(srcTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, original));
    }
    ExecuteAndWait(core, ctx);

    D3D12CpuImage image = CreateCpuImage(w, h, desc.Format);
    const auto& plane = image.planes[0];
    const auto* mapped = static_cast<const uint8_t*>(rb.Map()) + fp.layout.Offset;
    CopyRows(image.pixels.data() + plane.offsetBytes,
             plane.rowPitch,
             mapped,
             fp.layout.Footprint.RowPitch,
             static_cast<UINT>(fp.rowSize),
             fp.rows);
    rb.Unmap();
    return image;
}

D3D12CpuImage ReadbackTexture2DToCpuImage(D3D12Core& core, const D3D12Resource& srcTexture) {
    RequireTexture2D(srcTexture, "ReadbackTexture2DToCpuImage");
    const auto d = srcTexture.GetDesc();
    D3D12_BOX b = { 0, 0, 0, static_cast<UINT>(d.Width), d.Height, 1 };
    return ReadbackTexture2DRegionToCpuImage(core, srcTexture, b);
}

D3D12Resource CreateTexture2DFromCpuImage(D3D12Core& core, const D3D12CpuImage& image, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES finalState) {
    ValidateCpuImage(image, "CreateTexture2DFromCpuImage");
    D3D12Resource tex = CreateTexture2D(core, image.width, image.height, image.format, D3D12_RESOURCE_STATE_COPY_DEST, flags);
    UpdateTexture2DFromCpuImage(core, tex, image, finalState);
    return tex;
}

void UpdateTexture2DFromCpuImage(D3D12Core& core, D3D12Resource& dstTexture, const D3D12CpuImage& image, D3D12_RESOURCE_STATES finalState) {
    constexpr const char* fn = "UpdateTexture2DFromCpuImage";
    ValidateCpuImage(image, fn);
    RequireTexture2D(dstTexture, fn);
    const auto d = dstTexture.GetDesc();
    if (d.Width != image.width || d.Height != image.height || d.Format != image.format) {
        throw std::runtime_error(std::string(fn) + ": image and texture dimensions or format do not match");
    }

    D3D12UploadBuffer upload;
    upload.Initialize(core.GetDevice(), GetRequiredUploadSize(core, dstTexture));

    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    const auto original = dstTexture.GetState();
    if (original != D3D12_RESOURCE_STATE_COPY_DEST) {
        ctx.ResourceBarrier(MakeTransitionBarrier(dstTexture.Get(), original, D3D12_RESOURCE_STATE_COPY_DEST));
        dstTexture.SetState(D3D12_RESOURCE_STATE_COPY_DEST);
    }

    const auto& p = image.planes[0];
    RecordUploadTexture2D(core, ctx, dstTexture, upload, image.pixels.data() + p.offsetBytes, image.width, image.height, image.format, p.rowPitch, finalState);
    ExecuteAndWait(core, ctx);
    dstTexture.SetState(finalState);
}

} // namespace D3D12CoreLib
