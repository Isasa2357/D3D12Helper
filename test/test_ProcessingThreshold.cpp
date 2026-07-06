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
    if (std::filesystem::exists(runtimeDir / "ThresholdRgba.hlsl")) {
        return runtimeDir;
    }
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "ThresholdRgba.hlsl")) {
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

TEST(ProcessingThreshold, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    RequireProcessingShader(cache, "ThresholdRgba.hlsl");
    RequireProcessingShader(cache, "RangeThresholdRgba.hlsl");
    RequireProcessingShader(cache, "ConfidenceHeatmapRgba.hlsl");
    RequireProcessingShader(cache, "ClassColorMapRgba.hlsl");
    RequireProcessingShader(cache, "MaskOverlayRgba.hlsl");
}

TEST(ProcessingThreshold, ThresholdReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        0,   0, 0, 255,
        64,  0, 0, 255,
        128, 0, 0, 255,
        255, 0, 0, 255,
    };

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    ThresholdDesc desc = {};
    desc.channel = MaskChannel::Red;
    desc.threshold = 0.5f;
    desc.foregroundColor[0] = 255.0f / 255.0f;
    desc.foregroundColor[1] = 255.0f / 255.0f;
    desc.foregroundColor[2] = 255.0f / 255.0f;
    desc.foregroundColor[3] = 1.0f;
    desc.backgroundColor[0] = 0.0f;
    desc.backgroundColor[1] = 0.0f;
    desc.backgroundColor[2] = 0.0f;
    desc.backgroundColor[3] = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordThreshold(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const std::vector<uint8_t> expected = {
        0,   0,   0,   255,
        0,   0,   0,   255,
        255, 255, 255, 255,
        255, 255, 255, 255,
    };
    CheckBytesEqual(ReadbackCompactRgbaLike(readback), expected, "threshold readback");
}

TEST(ProcessingThreshold, RangeThresholdAndMaskOverlayReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        0,   0, 0, 255,
        96,  0, 0, 255,
        160, 0, 0, 255,
        255, 0, 0, 255,
    };

    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor processor;
    processor.Initialize(fx.context);

    auto rangeDst = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    RangeThresholdDesc range = {};
    range.channel = MaskChannel::Red;
    range.minValue = 0.25f;
    range.maxValue = 0.75f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordRangeThreshold(commandContext, src, rangeDst, range);
    auto readback = RecordReadbackTexture2D(*core, commandContext, rangeDst);
    ExecuteAndWait(*core, commandContext);

    const std::vector<uint8_t> expected = {
        0,   0,   0,   255,
        255, 255, 255, 255,
        255, 255, 255, 255,
        0,   0,   0,   255,
    };
    CheckBytesEqual(ReadbackCompactRgbaLike(readback), expected, "range threshold readback");
}

TEST(ProcessingThreshold, HeatmapAndClassColorMapReadback) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        0,   0, 0, 255,
        128, 0, 0, 255,
        255, 0, 0, 255,
        64,  0, 0, 255,
    };

    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor processor;
    processor.Initialize(fx.context);

    auto heatmap = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    ConfidenceHeatmapDesc hd = {};
    hd.channel = MaskChannel::Red;
    hd.mode = HeatmapMode::Grayscale;
    hd.opacity = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordConfidenceHeatmap(commandContext, src, heatmap, hd);
    auto rb = RecordReadbackTexture2D(*core, commandContext, heatmap);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(rb);
    CheckByteNear(got[0], 0, 1, "heatmap r0", 0);
    CheckByteNear(got[4], 128, 2, "heatmap r1", 4);
    CheckByteNear(got[8], 255, 1, "heatmap r2", 8);
    CheckByteNear(got[12], 64, 2, "heatmap r3", 12);
}
