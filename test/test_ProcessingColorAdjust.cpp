#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <algorithm>
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
    return std::filesystem::exists(dir / "ColorAdjustRgba.hlsl", ec) && !ec;
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

} // namespace

TEST(ProcessingColorAdjust, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    RequireProcessingShader(cache, "ColorAdjustRgba.hlsl");
}

TEST(ProcessingColorAdjust, IdentityReadbackMatchesPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT width = 2;
    const UINT height = 2;
    const std::vector<uint8_t> srcPixels = {
        10,  20,  30, 255,
        40,  50,  60, 240,
        70,  80,  90, 230,
        100, 110, 120, 220,
    };

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ColorAdjuster adjuster;
    adjuster.Initialize(fx.context);
    auto dst = adjuster.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    ColorAdjustDesc desc = {};
    desc.brightness = 0.0f;
    desc.contrast = 1.0f;
    desc.gamma = 1.0f;
    desc.saturation = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    adjuster.RecordColorAdjust(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto actual = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(actual, srcPixels, "identity color adjust");
}

TEST(ProcessingColorAdjust, SaturationZeroProducesGrayscale) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT width = 2;
    const UINT height = 1;
    const std::vector<uint8_t> srcPixels = {
        255,   0,   0, 255,
          0, 255,   0, 200,
    };

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ColorAdjuster adjuster;
    adjuster.Initialize(fx.context);
    auto dst = adjuster.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    ColorAdjustDesc desc = {};
    desc.saturation = 0.0f;
    desc.preserveAlpha = true;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    adjuster.RecordColorAdjust(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto actual = ReadbackCompactRgbaLike(readback);
    CHECK_EQ(actual.size(), srcPixels.size());

    // BT.709 luma from the shader: red ~= 54, green ~= 182.
    CheckByteNear(actual[0], 54, 2, "red grayscale r", 0);
    CheckByteNear(actual[1], 54, 2, "red grayscale g", 1);
    CheckByteNear(actual[2], 54, 2, "red grayscale b", 2);
    CHECK_EQ(actual[3], 255);

    CheckByteNear(actual[4], 182, 2, "green grayscale r", 4);
    CheckByteNear(actual[5], 182, 2, "green grayscale g", 5);
    CheckByteNear(actual[6], 182, 2, "green grayscale b", 6);
    CHECK_EQ(actual[7], 200);
}

TEST(ProcessingColorAdjust, ValidationRejectsInvalidDesc) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    std::vector<uint8_t> pixels(4u * 4u * 4u, 255);
    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
        4,
        4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ColorAdjuster adjuster;
    adjuster.Initialize(fx.context);
    auto dst = adjuster.CreateOutputTexture(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();

    ColorAdjustDesc desc = {};
    desc.gamma = 0.0f;
    CHECK_THROWS(adjuster.RecordColorAdjust(commandContext, src, dst, desc));

    desc = {};
    desc.contrast = -1.0f;
    CHECK_THROWS(adjuster.RecordColorAdjust(commandContext, src, dst, desc));

    desc = {};
    desc.saturation = -1.0f;
    CHECK_THROWS(adjuster.RecordColorAdjust(commandContext, src, dst, desc));
}
