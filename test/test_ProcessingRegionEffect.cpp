#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasRegionEffectShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "RegionEffectRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasRegionEffectShader(runtimeDir)) {
        return runtimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasRegionEffectShader(sourceDir)) {
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

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c)
        : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);
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

    if (!texture.Get()) {
        TEST_FAIL("RecordReadbackTexture2D: null texture");
    }

    const auto desc = texture.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        TEST_FAIL("RecordReadbackTexture2D: resource is not Texture2D");
    }
    if (desc.DepthOrArraySize != 1 || desc.MipLevels != 1) {
        TEST_FAIL("RecordReadbackTexture2D: only single mip / single array textures are supported");
    }

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
    if (rb.rowSize < bytesPerRow) {
        TEST_FAIL("ReadbackCompactRgbaLike: rowSize is smaller than width * 4");
    }
    if (rb.numRows < rb.height) {
        TEST_FAIL("ReadbackCompactRgbaLike: numRows is smaller than texture height");
    }

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

std::vector<uint8_t> MakeSolidRgba(UINT width, UINT height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return pixels;
}

} // namespace

TEST(ProcessingRegionEffect, ShaderCompile) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);

    CHECK(!cache.GetComputeShader("RegionEffectRgba.hlsl").Empty());
}

TEST(ProcessingRegionEffect, CreateOutputTexture) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    D3D12RegionEffectProcessor processor;
    processor.Initialize(fx.context);

    auto dst = processor.CreateOutputTexture(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(dst.Get() != nullptr);
    CHECK_EQ(dst.GetFormat(), DXGI_FORMAT_R8G8B8A8_UNORM);

    CHECK_THROWS(processor.CreateOutputTexture(*core, 16, 16, DXGI_FORMAT_NV12));
    CHECK_THROWS(processor.CreateOutputTexture(*core, 0, 16, DXGI_FORMAT_R8G8B8A8_UNORM));
}

TEST(ProcessingRegionEffect, DarkenOutsideCircleReadback) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    constexpr UINT width = 4;
    constexpr UINT height = 4;
    const auto srcPixels = MakeSolidRgba(width, height, 100, 120, 140, 255);

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12RegionEffectProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    RegionEffectDesc desc = {};
    desc.shape = RegionShape::Circle;
    desc.selection = RegionSelection::Outside;
    desc.effect = RegionEffectMode::Darken;
    desc.centerX = 2.0f;
    desc.centerY = 2.0f;
    desc.radius = 1.0f;
    desc.edgeSoftness = 0.0f;
    desc.strength = 0.5f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordRegionEffect(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);

    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            const bool insideCenterQuad = (x == 1 || x == 2) && (y == 1 || y == 2);
            const uint8_t expectedR = insideCenterQuad ? 100 : 50;
            const uint8_t expectedG = insideCenterQuad ? 120 : 60;
            const uint8_t expectedB = insideCenterQuad ? 140 : 70;

            CheckByteNear(got[i + 0], expectedR, 1, "region darken r", i + 0);
            CheckByteNear(got[i + 1], expectedG, 1, "region darken g", i + 1);
            CheckByteNear(got[i + 2], expectedB, 1, "region darken b", i + 2);
            CheckByteNear(got[i + 3], 255, 1, "region darken a", i + 3);
        }
    }
}

TEST(ProcessingRegionEffect, TintInsideRectReadback) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    constexpr UINT width = 4;
    constexpr UINT height = 4;
    const auto srcPixels = MakeSolidRgba(width, height, 20, 40, 80, 255);

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12RegionEffectProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    RegionEffectDesc desc = {};
    desc.shape = RegionShape::Rect;
    desc.selection = RegionSelection::Inside;
    desc.effect = RegionEffectMode::Tint;
    desc.rectX = 1.0f;
    desc.rectY = 1.0f;
    desc.rectWidth = 2.0f;
    desc.rectHeight = 2.0f;
    desc.edgeSoftness = 0.0f;
    desc.strength = 1.0f;
    desc.tintColor[0] = 1.0f;
    desc.tintColor[1] = 0.0f;
    desc.tintColor[2] = 0.0f;
    desc.tintColor[3] = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordRegionEffect(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);

    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            const bool inside = (x == 1 || x == 2) && (y == 1 || y == 2);
            const uint8_t expectedR = inside ? 255 : 20;
            const uint8_t expectedG = inside ? 0 : 40;
            const uint8_t expectedB = inside ? 0 : 80;

            CheckByteNear(got[i + 0], expectedR, 1, "region tint r", i + 0);
            CheckByteNear(got[i + 1], expectedG, 1, "region tint g", i + 1);
            CheckByteNear(got[i + 2], expectedB, 1, "region tint b", i + 2);
            CheckByteNear(got[i + 3], 255, 1, "region tint a", i + 3);
        }
    }
}
