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
    return std::filesystem::exists(dir / "RemapRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(runtimeDir)) {
        return runtimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(sourceDir)) {
        return sourceDir;
    }
#endif

    return runtimeDir;
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& ctx) {
    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

std::vector<uint8_t> ReadbackTextureRgba8(D3D12Core& core, D3D12Resource& texture) {
    const auto desc = texture.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)) {
        TEST_FAIL("ReadbackTextureRgba8: expected RGBA-like Texture2D");
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    core.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSize, &totalBytes);

    D3D12ReadbackBuffer readback;
    readback.Initialize(core.GetDevice(), totalBytes);

    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    if (texture.GetState() != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        ctx.ResourceBarrier(MakeTransitionBarrier(
            texture.Get(),
            texture.GetState(),
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        texture.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = texture.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readback.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = layout;

    ctx.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    ExecuteAndWait(core, ctx);

    const UINT width = static_cast<UINT>(desc.Width);
    const UINT height = desc.Height;
    const UINT bytesPerPixel = 4;
    std::vector<uint8_t> out(static_cast<size_t>(width) * height * bytesPerPixel);

    const auto* mapped = static_cast<const uint8_t*>(readback.Map());
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(
            out.data() + static_cast<size_t>(y) * width * bytesPerPixel,
            mapped + layout.Offset + static_cast<size_t>(y) * layout.Footprint.RowPitch,
            static_cast<size_t>(width) * bytesPerPixel);
    }
    readback.Unmap();
    return out;
}

void RequireByteNear(uint8_t actual, uint8_t expected, uint8_t tolerance, const char* label, size_t index) {
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

void RequireBytesEqual(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, const char* label) {
    CHECK_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::ostringstream os;
            os << label << ": byte mismatch at index " << i
               << " actual=" << static_cast<int>(actual[i])
               << " expected=" << static_cast<int>(expected[i]);
            TEST_FAIL(os.str());
        }
    }
}

} // namespace

TEST(Processing, RemapAndCompositeShadersCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    try {
        CHECK(!cache.GetComputeShader("RemapRgba.hlsl").Empty());
        CHECK(!cache.GetComputeShader("CompositeRgba.hlsl").Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile remap/composite shader: ") + e.what());
    }
}

TEST(Processing, RemapPointReadbackFlipsHorizontally) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT w = 2;
    const UINT h = 2;
    const std::vector<uint8_t> srcPixels = {
        255,   0,   0, 255,     0, 255,   0, 255,
          0,   0, 255, 255,   255, 255, 255, 255,
    };
    auto src = CreateTexture2DFromRGBA(
        *core, srcPixels.data(), w, h, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const std::vector<float> mapPixels = {
        1.0f, 0.0f,   0.0f, 0.0f,
        1.0f, 1.0f,   0.0f, 1.0f,
    };
    auto map = CreateTexture2DFromMemory(
        *core,
        mapPixels.data(),
        w,
        h,
        DXGI_FORMAT_R32G32_FLOAT,
        w * sizeof(float) * 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Remapper remapper;
    remapper.Initialize(fx.context);
    auto dst = remapper.CreateOutputTexture(*core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);

    RemapDesc desc = {};
    desc.filter = ProcessingFilter::Point;
    desc.coordinateMode = RemapCoordinateMode::AbsolutePixels;
    desc.borderMode = RemapBorderMode::Clamp;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    remapper.RecordRemap(ctx, src, map, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackTextureRgba8(*core, dst);
    const std::vector<uint8_t> expected = {
          0, 255,   0, 255,   255,   0,   0, 255,
        255, 255, 255, 255,     0,   0, 255, 255,
    };
    RequireBytesEqual(got, expected, "remap horizontal flip");
}

TEST(Processing, CompositeAlphaBlendReadbackMatchesExpectedPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT w = 2;
    const UINT h = 2;
    const std::vector<uint8_t> basePixels = {
        100, 0, 0, 255,  100, 0, 0, 255,
        100, 0, 0, 255,  100, 0, 0, 255,
    };
    const std::vector<uint8_t> overlayPixels = {
        0, 0, 200, 128,  0, 0, 200, 128,
        0, 0, 200, 128,  0, 0, 200, 128,
    };

    auto base = CreateTexture2DFromRGBA(
        *core, basePixels.data(), w, h, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto overlay = CreateTexture2DFromRGBA(
        *core, overlayPixels.data(), w, h, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Compositor compositor;
    compositor.Initialize(fx.context);
    auto dst = compositor.CreateOutputTexture(*core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);

    CompositeDesc desc = {};
    desc.blendMode = CompositeBlendMode::AlphaBlend;
    desc.opacity = 1.0f;

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    compositor.RecordComposite(ctx, base, overlay, dst, desc);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackTextureRgba8(*core, dst);
    const std::vector<uint8_t> expected = {
        50, 0, 100, 255,  50, 0, 100, 255,
        50, 0, 100, 255,  50, 0, 100, 255,
    };

    CHECK_EQ(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i) {
        RequireByteNear(got[i], expected[i], 1, "composite alpha blend", i);
    }
}
