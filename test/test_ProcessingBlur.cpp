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

bool HasBlurShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "GaussianBlurHorizontalRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasBlurShader(runtimeDir)) {
        return runtimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasBlurShader(sourceDir)) {
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

void CheckBytesEqual(
    const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& expected,
    const char* label) {

    if (actual.size() != expected.size()) {
        std::ostringstream os;
        os << label << ": size mismatch actual=" << actual.size()
           << " expected=" << expected.size();
        TEST_FAIL(os.str());
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::ostringstream os;
            os << label << ": byte mismatch at index " << i
               << " actual=" << static_cast<int>(actual[i])
               << " expected=" << static_cast<int>(expected[i]);
            TEST_FAIL(os.str());
        }
    }
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

std::vector<uint8_t> MakeCenterImpulse3x3() {
    std::vector<uint8_t> pixels(3u * 3u * 4u, 0);

    const size_t center = static_cast<size_t>(1u * 3u + 1u) * 4u;
    pixels[center + 0] = 255;
    pixels[center + 1] = 0;
    pixels[center + 2] = 0;
    pixels[center + 3] = 255;

    return pixels;
}

} // namespace

TEST(ProcessingBlur, ShaderCompile) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);

    CHECK(!cache.GetComputeShader("GaussianBlurHorizontalRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("GaussianBlurVerticalRgba.hlsl").Empty());
}

TEST(ProcessingBlur, CreateTextures) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    D3D12Blurrer blurrer;
    blurrer.Initialize(fx.context);

    auto scratch = blurrer.CreateScratchTexture(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = blurrer.CreateOutputTexture(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM);

    CHECK(scratch.Get() != nullptr);
    CHECK(dst.Get() != nullptr);
    CHECK_EQ(scratch.GetFormat(), DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK_EQ(dst.GetFormat(), DXGI_FORMAT_R8G8B8A8_UNORM);

    CHECK_THROWS(blurrer.CreateScratchTexture(*core, 16, 16, DXGI_FORMAT_NV12));
    CHECK_THROWS(blurrer.CreateOutputTexture(*core, 0, 16, DXGI_FORMAT_R8G8B8A8_UNORM));
}

TEST(ProcessingBlur, GaussianRadiusZeroCopies) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        10, 20, 30, 255,   40, 50, 60, 240,
        70, 80, 90, 230,   100, 110, 120, 220,
    };

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Blurrer blurrer;
    blurrer.Initialize(fx.context);

    auto scratch = blurrer.CreateScratchTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = blurrer.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    BlurDesc desc = {};
    desc.mode = BlurMode::Gaussian;
    desc.radius = 0;
    desc.sigma = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    blurrer.RecordBlur(commandContext, src, scratch, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    auto actual = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(actual, srcPixels, "D3D12 blur radius zero readback");
}

TEST(ProcessingBlur, BoxBlurImpulseReadback) {
    REQUIRE_CORE(core);

    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto srcPixels = MakeCenterImpulse3x3();

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        3,
        3,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Blurrer blurrer;
    blurrer.Initialize(fx.context);

    auto scratch = blurrer.CreateScratchTexture(*core, 3, 3, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = blurrer.CreateOutputTexture(*core, 3, 3, DXGI_FORMAT_R8G8B8A8_UNORM);

    BlurDesc desc = {};
    desc.mode = BlurMode::Box;
    desc.radius = 1;
    desc.edgeMode = BlurEdgeMode::Constant;
    desc.borderColor[0] = 0.0f;
    desc.borderColor[1] = 0.0f;
    desc.borderColor[2] = 0.0f;
    desc.borderColor[3] = 0.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    blurrer.RecordBlur(commandContext, src, scratch, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);

    for (size_t i = 0; i < got.size(); i += 4) {
        CheckByteNear(got[i + 0], 28, 2, "box blur r", i + 0);
        CheckByteNear(got[i + 1], 0, 1, "box blur g", i + 1);
        CheckByteNear(got[i + 2], 0, 1, "box blur b", i + 2);
        CheckByteNear(got[i + 3], 28, 2, "box blur a", i + 3);
    }
}
