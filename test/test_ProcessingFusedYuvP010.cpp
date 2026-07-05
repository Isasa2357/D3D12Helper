#include "TestCommon.hpp"
#include "D3D12Processing/D3D12Processing.hpp"
#include "D3D12Core/D3D12Barrier.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "FusedYuv420ToRgbResize.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(runtimeDir)) return runtimeDir;
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(sourceDir)) return sourceDir;
#endif
    return runtimeDir;
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

void CheckByteNear(uint8_t actual, uint8_t expected, uint8_t tolerance, const char* label, size_t index) {
    const int diff = (actual > expected) ? (actual - expected) : (expected - actual);
    if (diff > tolerance) {
        std::ostringstream os;
        os << label << ": byte mismatch at index " << index
           << " actual=" << static_cast<int>(actual)
           << " expected=" << static_cast<int>(expected)
           << " tolerance=" << static_cast<int>(tolerance);
        TEST_FAIL(os.str());
    }
}

void CheckBytesNear(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, uint8_t tolerance, const char* label) {
    CHECK_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        CheckByteNear(actual[i], expected[i], tolerance, label, i);
    }
}

D3D12Resource CreateUploadedYuv420(
    D3D12Core& core,
    DXGI_FORMAT format,
    UINT width,
    UINT height,
    const void* yData,
    UINT64 yRowPitch,
    const void* uvData,
    UINT64 uvRowPitch,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {

    auto tex = CreateTexture2D(core, width, height, format, D3D12_RESOURCE_STATE_COPY_DEST, flags);
    D3D12UploadBuffer upload;
    upload.Initialize(core.GetDevice(), GetRequiredUploadSize(core, tex, 0, 2));

    D3D12TextureSubresourceData subresources[2] = {};
    subresources[0].data = yData;
    subresources[0].rowPitch = yRowPitch;
    subresources[1].data = uvData;
    subresources[1].rowPitch = uvRowPitch;

    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    RecordUploadTextureSubresources(core, ctx, tex, upload, subresources, 0, 2, finalState);
    ExecuteAndWait(core, ctx);
    tex.SetState(finalState);
    return tex;
}

std::vector<uint8_t> ReadbackSubresourceCompact(
    D3D12Core& core,
    D3D12Resource& texture,
    UINT subresourceIndex,
    UINT rowBytes,
    UINT rows) {

    const auto desc = texture.GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    core.GetDevice()->GetCopyableFootprints(&desc, subresourceIndex, 1, 0, &layout, &numRows, &rowSize, &totalBytes);
    if (numRows < rows || rowSize < rowBytes) {
        TEST_FAIL("ReadbackSubresourceCompact: invalid footprint for requested compact copy");
    }

    D3D12ReadbackBuffer readback;
    readback.Initialize(core.GetDevice(), totalBytes);

    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    if (texture.GetState() != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        ctx.ResourceBarrier(MakeTransitionBarrier(texture.Get(), texture.GetState(), D3D12_RESOURCE_STATE_COPY_SOURCE));
        texture.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = subresourceIndex;

    ctx.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    ExecuteAndWait(core, ctx);

    std::vector<uint8_t> out(static_cast<size_t>(rowBytes) * rows);
    const auto* mapped = static_cast<const uint8_t*>(readback.Map());
    const auto* base = mapped + layout.Offset;
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * rowBytes,
                    base + static_cast<size_t>(y) * layout.Footprint.RowPitch,
                    rowBytes);
    }
    readback.Unmap();
    return out;
}

std::vector<uint8_t> ReadbackRgba8(D3D12Core& core, D3D12Resource& texture) {
    const auto desc = texture.GetDesc();
    return ReadbackSubresourceCompact(core, texture, 0, static_cast<UINT>(desc.Width) * 4u, desc.Height);
}

std::vector<uint8_t> Expand2x2To4x4Gray(const std::array<uint8_t, 4>& y) {
    std::vector<uint8_t> expected(4u * 4u * 4u);
    for (UINT dy = 0; dy < 4; ++dy) {
        for (UINT dx = 0; dx < 4; ++dx) {
            const UINT sx = dx / 2u;
            const UINT sy = dy / 2u;
            const uint8_t g = y[sy * 2u + sx];
            const size_t i = static_cast<size_t>(dy * 4u + dx) * 4u;
            expected[i + 0] = g;
            expected[i + 1] = g;
            expected[i + 2] = g;
            expected[i + 3] = 255;
        }
    }
    return expected;
}

} // namespace

TEST(Processing, FusedAndYuvShadersCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    try {
        CHECK(!cache.GetComputeShader("FusedRgbToRgbResize.hlsl").Empty());
        CHECK(!cache.GetComputeShader("FusedYuv420ToRgbResize.hlsl").Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile fused processing shader: ") + e.what());
    }
}

TEST(Processing, Nv12ToRgbaReadbackMatchesNeutralGray) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsNv12Srv() || !fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("NV12 SRV or R8G8B8A8 UAV is not supported");
    }

    const UINT w = 2;
    const UINT h = 2;
    const std::array<uint8_t, 4> y = { 0, 85, 170, 255 };
    const std::array<uint8_t, 2> uv = { 128, 128 };
    auto src = CreateUploadedYuv420(*core, DXGI_FORMAT_NV12, w, h, y.data(), w, uv.data(), w);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(*core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);

    FormatConvertDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_NV12;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.color.srcRange = ProcessingColorRange::Full;
    desc.color.srcMatrix = ProcessingColorMatrix::BT709;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    converter.RecordConvert(ctx, src, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackRgba8(*core, dst);
    const std::vector<uint8_t> expected = {
          0,   0,   0, 255,  85,  85,  85, 255,
        170, 170, 170, 255, 255, 255, 255, 255,
    };
    CheckBytesNear(got, expected, 1, "NV12 -> RGBA neutral gray");
}

TEST(Processing, RgbaToNv12ReadbackMatchesNeutralGray) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsNv12Uav()) {
        TEST_SKIP("NV12 UAV plane views are not supported");
    }

    const UINT w = 2;
    const UINT h = 2;
    const std::vector<uint8_t> srcPixels = {
          0,   0,   0, 255,   64,  64,  64, 255,
        128, 128, 128, 255,  255, 255, 255, 255,
    };
    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), w, h, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(*core, w, h, DXGI_FORMAT_NV12);

    FormatConvertDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_NV12;
    desc.color.dstRange = ProcessingColorRange::Full;
    desc.color.dstMatrix = ProcessingColorMatrix::BT709;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    converter.RecordConvert(ctx, src, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto y = ReadbackSubresourceCompact(*core, dst, 0, w, h);
    const auto uv = ReadbackSubresourceCompact(*core, dst, 1, w, h / 2);

    const std::vector<uint8_t> expectedY = { 0, 64, 128, 255 };
    const std::vector<uint8_t> expectedUv = { 128, 128 };
    CheckBytesNear(y, expectedY, 1, "RGBA -> NV12 Y plane");
    CheckBytesNear(uv, expectedUv, 1, "RGBA -> NV12 UV plane");
}

TEST(Processing, P010ToRgbaReadbackMatchesNeutralGray) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsP010Srv() || !fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("P010 SRV or R8G8B8A8 UAV is not supported");
    }

    const UINT w = 2;
    const UINT h = 2;
    const std::array<uint16_t, 4> y = { 0, 21845, 43690, 65535 };
    const std::array<uint16_t, 2> uv = { 32768, 32768 };
    auto src = CreateUploadedYuv420(
        *core,
        DXGI_FORMAT_P010,
        w,
        h,
        y.data(),
        static_cast<UINT64>(w) * sizeof(uint16_t),
        uv.data(),
        static_cast<UINT64>(w) * sizeof(uint16_t));

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(*core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);

    FormatConvertDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_P010;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.color.srcRange = ProcessingColorRange::Full;
    desc.color.srcMatrix = ProcessingColorMatrix::BT709;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    converter.RecordConvert(ctx, src, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackRgba8(*core, dst);
    const std::vector<uint8_t> expected = {
          0,   0,   0, 255,  85,  85,  85, 255,
        170, 170, 170, 255, 255, 255, 255, 255,
    };
    CheckBytesNear(got, expected, 2, "P010 -> RGBA neutral gray");
}

TEST(Processing, Rgba16FloatViewAndOutputAreSupportedWhenDeviceSupportsTypedStore) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba16FloatUav()) {
        TEST_SKIP("R16G16B16A16_FLOAT UAV typed store is not supported");
    }

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto out = converter.CreateOutputTexture(*core, 4, 4, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CHECK(out.Get() != nullptr);
    CHECK_EQ(out.GetFormat(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    auto views = CreateRgbaTextureViewSet(fx.context, out, true, true, DXGI_FORMAT_R16G16B16A16_FLOAT);
    CHECK(views.HasSrv());
    CHECK(views.HasUav());
}

TEST(Processing, FusedNv12ToRgbaPointResizeReadbackMatchesExpectedPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsNv12Srv() || !fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("NV12 SRV or R8G8B8A8 UAV is not supported");
    }

    const UINT srcW = 2;
    const UINT srcH = 2;
    const UINT dstW = 4;
    const UINT dstH = 4;
    const std::array<uint8_t, 4> y = { 10, 60, 120, 240 };
    const std::array<uint8_t, 2> uv = { 128, 128 };
    auto src = CreateUploadedYuv420(*core, DXGI_FORMAT_NV12, srcW, srcH, y.data(), srcW, uv.data(), srcW);

    D3D12FusedProcessor fused;
    fused.Initialize(fx.context);
    auto dst = fused.CreateOutputTexture(*core, dstW, dstH, DXGI_FORMAT_R8G8B8A8_UNORM);

    FusedConvertResizeDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_NV12;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.filter = ProcessingFilter::Point;
    desc.color.srcRange = ProcessingColorRange::Full;
    desc.color.srcMatrix = ProcessingColorMatrix::BT709;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    fused.RecordConvertResize(ctx, src, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackRgba8(*core, dst);
    const auto expected = Expand2x2To4x4Gray(y);
    CheckBytesNear(got, expected, 1, "fused NV12 -> RGBA point resize");
}

