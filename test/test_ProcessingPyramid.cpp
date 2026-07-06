#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

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

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(runtimeDir / "PyramidDownsample2xRgba.hlsl")) {
        return runtimeDir;
    }
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "PyramidDownsample2xRgba.hlsl")) {
        return sourceDir;
    }
#endif
    return runtimeDir;
}

void RequireProcessingShader(D3D12ProcessingShaderCache& cache, const char* fileName) {
    try {
        const auto& bytecode = cache.GetComputeShader(fileName);
        CHECK(!bytecode.Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile processing shader ") + fileName + ": " + e.what());
    }
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

struct TextureReadback {
    D3D12ReadbackBuffer buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    UINT width = 0;
    UINT height = 0;
};

TextureReadback RecordReadbackTexture2D(
    D3D12Core& core,
    D3D12CommandContext& commandContext,
    D3D12Resource& texture) {

    const auto desc = texture.GetDesc();
    TextureReadback rb;
    rb.width = static_cast<UINT>(desc.Width);
    rb.height = desc.Height;
    core.GetDevice()->GetCopyableFootprints(
        &desc,
        0,
        1,
        0,
        &rb.layout,
        &rb.numRows,
        &rb.rowSize,
        &rb.totalBytes);

    rb.buffer.Initialize(core.GetDevice(), rb.totalBytes);

    const auto before = texture.GetState();
    if (before != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        commandContext.ResourceBarrier(MakeTransitionBarrier(
            texture.Get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE));
        texture.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = rb.buffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = rb.layout;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    commandContext.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    return rb;
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

std::vector<uint8_t> ReadbackCompactRgbaLike(TextureReadback& rb) {
    const UINT bytesPerRow = rb.width * 4u;
    std::vector<uint8_t> out(static_cast<size_t>(bytesPerRow) * rb.height);
    const auto* mapped = static_cast<const uint8_t*>(rb.buffer.Map());
    const auto* base = mapped + rb.layout.Offset;
    for (UINT y = 0; y < rb.height; ++y) {
        std::memcpy(
            out.data() + static_cast<size_t>(y) * bytesPerRow,
            base + static_cast<size_t>(y) * rb.layout.Footprint.RowPitch,
            bytesPerRow);
    }
    rb.buffer.Unmap();
    return out;
}

void CheckByteNear(uint8_t actual, uint8_t expected, uint8_t tolerance, const char* label, size_t index) {
    const int diff = actual > expected ? actual - expected : expected - actual;
    if (diff > tolerance) {
        std::ostringstream os;
        os << label << ": byte mismatch at index " << index
           << " actual=" << static_cast<int>(actual)
           << " expected=" << static_cast<int>(expected)
           << " tolerance=" << static_cast<int>(tolerance);
        TEST_FAIL(os.str());
    }
}

std::vector<uint8_t> MakeQuadrant4x4() {
    std::vector<uint8_t> pixels(4u * 4u * 4u, 0);
    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const size_t i = static_cast<size_t>(y * 4u + x) * 4u;
            const uint8_t v = (y < 2)
                ? ((x < 2) ? 10 : 40)
                : ((x < 2) ? 70 : 100);
            pixels[i + 0] = v;
            pixels[i + 1] = static_cast<uint8_t>(v + 1);
            pixels[i + 2] = static_cast<uint8_t>(v + 2);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

std::vector<uint8_t> MakePointUpsampleExpected2x2To4x4(const std::vector<uint8_t>& src) {
    std::vector<uint8_t> expected(4u * 4u * 4u, 0);
    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const UINT sx = x / 2u;
            const UINT sy = y / 2u;
            std::memcpy(
                expected.data() + static_cast<size_t>(y * 4u + x) * 4u,
                src.data() + static_cast<size_t>(sy * 2u + sx) * 4u,
                4u);
        }
    }
    return expected;
}

} // namespace

TEST(ProcessingPyramid, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    RequireProcessingShader(cache, "PyramidDownsample2xRgba.hlsl");
    RequireProcessingShader(cache, "PyramidUpsample2xRgba.hlsl");
}

TEST(ProcessingPyramid, Downsample2xReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto srcPixels = MakeQuadrant4x4();
    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 4, 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidProcessor pyramid;
    pyramid.Initialize(fx.context);
    auto dst = pyramid.CreateDownsampledTexture(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    PyramidDownsampleDesc desc = {};

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    pyramid.RecordDownsample2x(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);
    const std::vector<uint8_t> expected = {
        10, 11, 12, 255,   40, 41, 42, 255,
        70, 71, 72, 255,   100, 101, 102, 255,
    };

    CHECK_EQ(got.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        CheckByteNear(got[i], expected[i], 1, "downsample2x", i);
    }
}

TEST(ProcessingPyramid, Upsample2xPointReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        10, 20, 30, 255,   40, 50, 60, 128,
        70, 80, 90, 255,   100, 110, 120, 64,
    };

    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidProcessor pyramid;
    pyramid.Initialize(fx.context);
    auto dst = pyramid.CreateUpsampledTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    PyramidUpsampleDesc desc = {};
    desc.filter = ProcessingFilter::Point;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    pyramid.RecordUpsample2x(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);
    const auto expected = MakePointUpsampleExpected2x2To4x4(srcPixels);

    CHECK_EQ(got.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        CheckByteNear(got[i], expected[i], 1, "upsample2x point", i);
    }
}

TEST(ProcessingPyramid, DownsampleOddSizeClampReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        10, 0, 0, 255,   20, 0, 0, 255,   30, 0, 0, 255,
        40, 0, 0, 255,   50, 0, 0, 255,   60, 0, 0, 255,
        70, 0, 0, 255,   80, 0, 0, 255,   90, 0, 0, 255,
    };

    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 3, 3, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidProcessor pyramid;
    pyramid.Initialize(fx.context);
    auto dst = pyramid.CreateDownsampledTexture(*core, 3, 3, DXGI_FORMAT_R8G8B8A8_UNORM);

    PyramidDownsampleDesc desc = {};
    desc.edgeMode = PyramidEdgeMode::Clamp;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    pyramid.RecordDownsample2x(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);

    const std::vector<uint8_t> expectedR = { 30, 45, 75, 90 };
    for (size_t pixel = 0; pixel < expectedR.size(); ++pixel) {
        const size_t i = pixel * 4u;
        CheckByteNear(got[i + 0], expectedR[pixel], 1, "odd downsample r", i + 0);
        CheckByteNear(got[i + 1], 0, 1, "odd downsample g", i + 1);
        CheckByteNear(got[i + 2], 0, 1, "odd downsample b", i + 2);
        CHECK_EQ(got[i + 3], 255);
    }
}
