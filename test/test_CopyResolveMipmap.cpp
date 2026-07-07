#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cstdint>
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
    CHECK(!IsMultisampledTexture(src));
    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_THROWS(RecordResolveSubresource(ctx, dst, 0, src, 0));
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
