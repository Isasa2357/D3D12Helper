#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cstdint>
#include <sstream>
#include <vector>

using namespace D3D12CoreLib;

namespace {

D3D12CpuImage MakeImage(UINT width, UINT height, uint8_t base) {
    auto img = CreateCpuImage(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto& p = img.planes[0];
    for (UINT y = 0; y < height; ++y) {
        auto* row = img.pixels.data() + p.offsetBytes + static_cast<size_t>(y) * p.rowPitch;
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(x) * 4u;
            row[i + 0] = static_cast<uint8_t>(base + x + y * 11u);
            row[i + 1] = static_cast<uint8_t>(base + 40u + x + y * 7u);
            row[i + 2] = static_cast<uint8_t>(base + 80u + x + y * 5u);
            row[i + 3] = 255;
        }
    }
    return img;
}

void CheckPixelEq(const D3D12CpuImage& a, UINT ax, UINT ay, const D3D12CpuImage& b, UINT bx, UINT by) {
    const auto& ap = a.planes[0];
    const auto& bp = b.planes[0];
    const auto* pa = a.pixels.data() + ap.offsetBytes + static_cast<size_t>(ay) * ap.rowPitch + static_cast<size_t>(ax) * 4u;
    const auto* pb = b.pixels.data() + bp.offsetBytes + static_cast<size_t>(by) * bp.rowPitch + static_cast<size_t>(bx) * 4u;
    for (UINT i = 0; i < 4; ++i) CHECK_EQ(static_cast<int>(pa[i]), static_cast<int>(pb[i]));
}

void RequireByteNear(uint8_t actual, uint8_t expected, uint8_t tolerance, const char* label) {
    const int diff = actual > expected ? actual - expected : expected - actual;
    if (diff > tolerance) {
        std::ostringstream os;
        os << label << ": actual=" << int(actual)
           << " expected=" << int(expected)
           << " tolerance=" << int(tolerance);
        TEST_FAIL(os.str());
    }
}

bool SupportsMsaa(ID3D12Device* device, DXGI_FORMAT format, UINT sampleCount) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = format;
    levels.SampleCount = sampleCount;
    levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels)))) {
        return false;
    }
    return levels.NumQualityLevels > 0;
}

D3D12Resource CreateMsaaRenderTarget(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT sampleCount,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_RENDER_TARGET) {

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = sampleCount;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = format;
    clear.Color[0] = 0.0f;
    clear.Color[1] = 0.0f;
    clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        &clear,
        IID_PPV_ARGS(&resource)));
    return D3D12Resource(std::move(resource), initialState);
}

} // namespace

TEST(CopyResolveMipmap, TextureRegionCopy) {
    REQUIRE_CORE(core);

    auto srcImage = MakeImage(4, 3, 10);
    auto src = CreateTexture2DFromCpuImage(*core, srcImage);
    auto dst = CreateTexture2D(*core, 4, 3, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);

    D3D12_BOX box = { 1, 1, 0, 3, 3, 1 };
    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    RecordCopyTextureRegion(ctx, dst, 0, 1, 0, src, 0, box,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();

    auto got = ReadbackTexture2DToCpuImage(*core, dst);
    CheckPixelEq(got, 1, 0, srcImage, 1, 1);
    CheckPixelEq(got, 2, 0, srcImage, 2, 1);
    CheckPixelEq(got, 1, 1, srcImage, 1, 2);
    CheckPixelEq(got, 2, 1, srcImage, 2, 2);
}

TEST(CopyResolveMipmap, CopyValidation) {
    REQUIRE_CORE(core);
    auto a = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto b = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    D3D12_BOX empty = { 1, 1, 0, 1, 2, 1 };
    CHECK_THROWS(RecordCopyTextureRegion(ctx, b, 0, 0, 0, a, 0, empty));
}

TEST(CopyResolveMipmap, ResolveValidation) {
    REQUIRE_CORE(core);
    auto src = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto dst = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto dstSmall = CreateTexture2D(*core, 2, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto buffer = CreateBuffer(*core, 128, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    D3D12Resource nullResource;

    CHECK(!IsMultisampledTexture(src));
    CHECK(!IsMultisampledTexture(nullResource));

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_THROWS(RecordResolveSubresource(ctx, dst, 0, src, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, dst, 0, nullResource, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, nullResource, 0, src, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, dst, 0, buffer, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, buffer, 0, src, 0));

    if (SupportsMsaa(core->GetDevice(), DXGI_FORMAT_R8G8B8A8_UNORM, 4)) {
        auto msaa = CreateMsaaRenderTarget(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        auto msaaDst = CreateMsaaRenderTarget(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        CHECK(IsMultisampledTexture(msaa));
        CHECK_THROWS(RecordResolveSubresource(ctx, msaaDst, 0, msaa, 0));
        CHECK_THROWS(RecordResolveSubresource(ctx, dstSmall, 0, msaa, 0));
    }
}

TEST(CopyResolveMipmap, ResolveMsaaRenderTargetToSingleSampleTexture) {
    REQUIRE_CORE(core);
    constexpr UINT kSampleCount = 4;
    constexpr UINT kSize = 8;
    constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!SupportsMsaa(core->GetDevice(), kFormat, kSampleCount)) {
        TEST_SKIP("R8G8B8A8 4x MSAA is not supported on this adapter");
    }

    auto src = CreateMsaaRenderTarget(*core, kSize, kSize, kFormat, kSampleCount);
    auto dst = CreateTexture2D(*core, kSize, kSize, kFormat, D3D12_RESOURCE_STATE_COMMON);

    D3D12DescriptorAllocator rtvAlloc;
    rtvAlloc.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    auto rtv = rtvAlloc.Allocate();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = kFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    core->GetDevice()->CreateRenderTargetView(src.Get(), &rtvDesc, rtv.cpu);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    const float clear[4] = { 0.25f, 0.50f, 0.75f, 1.0f };
    ctx.GetCommandList()->ClearRenderTargetView(rtv.cpu, clear, 0, nullptr);
    RecordResolveSubresource(
        ctx,
        dst,
        0,
        src,
        0,
        DXGI_FORMAT_UNKNOWN,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();

    CHECK_EQ(dst.GetState(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CHECK_EQ(src.GetState(), D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto got = ReadbackTexture2DToCpuImage(*core, dst);
    const auto& p = got.planes[0];
    const auto* center = got.pixels.data() + p.offsetBytes + static_cast<size_t>(kSize / 2) * p.rowPitch + static_cast<size_t>(kSize / 2) * 4u;
    RequireByteNear(center[0], 64, 2, "resolved R");
    RequireByteNear(center[1], 128, 2, "resolved G");
    RequireByteNear(center[2], 191, 3, "resolved B");
    RequireByteNear(center[3], 255, 1, "resolved A");
}

TEST(CopyResolveMipmap, MipmapUtilities) {
    CHECK_EQ(CalculateMipLevelCount(1, 1), 1u);
    CHECK_EQ(CalculateMipLevelCount(4, 4), 3u);
    CHECK_EQ(CalculateMipLevelCount(5, 3), 3u);
    auto mip = GetMipLevelInfo(4, 4, 2);
    CHECK_EQ(mip.width, 1u);
    CHECK_EQ(mip.height, 1u);
    CHECK(IsMipLevelValid(4, 4, 2));
    CHECK(!IsMipLevelValid(4, 4, 3));
    CHECK_THROWS(GetMipLevelInfo(4, 4, 3));
}

TEST(CopyResolveMipmap, CreateMipmappedTexture) {
    REQUIRE_CORE(core);
    auto tex = CreateMipmappedTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(IsMipmappedTexture(tex));
    CHECK_EQ(static_cast<UINT>(tex.GetDesc().MipLevels), 3u);
    CHECK_NOTHROW(ValidateMipmappedTexture(tex, "test"));
    CHECK_THROWS(CreateMipmappedTexture2D(*core, 4, 4, DXGI_FORMAT_UNKNOWN));
    CHECK_THROWS(CreateMipmappedTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 5));
}
