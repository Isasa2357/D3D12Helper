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

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "FusedRgbToRgbResize.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto namespacedRuntimeDir = std::filesystem::current_path() / "D3D12Helper" / "shaders" / "D3D12Processing";
    if (HasProcessingShader(namespacedRuntimeDir)) {
        return namespacedRuntimeDir;
    }

    const auto legacyRuntimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(legacyRuntimeDir)) {
        return legacyRuntimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(sourceDir)) {
        return sourceDir;
    }
#endif
    return namespacedRuntimeDir;
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

std::vector<uint8_t> ReadbackCompactRgba8(TextureReadback& rb) {
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

std::vector<uint8_t> MakePointResizeExpected2x2To4x4(const std::vector<uint8_t>& src) {
    std::vector<uint8_t> expected(4u * 4u * 4u);
    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const UINT sx = x / 2u;
            const UINT sy = y / 2u;
            const size_t srcIndex = static_cast<size_t>(sy * 2u + sx) * 4u;
            const size_t dstIndex = static_cast<size_t>(y * 4u + x) * 4u;
            std::memcpy(expected.data() + dstIndex, src.data() + srcIndex, 4u);
        }
    }
    return expected;
}

} // namespace

TEST(ProcessingFusedPipeline, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);

    CHECK(!cache.GetComputeShader("FusedRgbToRgbResize.hlsl").Empty());
    CHECK(!cache.GetComputeShader("FusedYuv420ToRgbResize.hlsl").Empty());
}

TEST(ProcessingFusedPipeline, RgbaPointResizeReadbackMatchesNearestPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        255,   0,   0, 255,   0, 255,   0, 255,
          0,   0, 255, 255, 255, 255,   0, 255,
    };
    const auto expected = MakePointResizeExpected2x2To4x4(srcPixels);

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FusedProcessor fused;
    fused.Initialize(fx.context);
    auto dst = fused.CreateOutputTexture(
        *core,
        4,
        4,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    FusedConvertResizeDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.filter = ProcessingFilter::Point;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    fused.RecordConvertResize(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    auto actual = ReadbackCompactRgba8(readback);
    CheckBytesEqual(actual, expected, "fused RGBA point resize 2x2 -> 4x4");
}

TEST(ProcessingFusedPipeline, RejectsInvalidOutputSize) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12FusedProcessor fused;
    fused.Initialize(fx.context);
    CHECK_THROWS(fused.CreateOutputTexture(*core, 0, 4, DXGI_FORMAT_R8G8B8A8_UNORM));
    CHECK_THROWS(fused.CreateOutputTexture(*core, 4, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
}
