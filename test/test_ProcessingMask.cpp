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

bool HasMaskShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "MaskApplyRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasMaskShader(runtimeDir)) return runtimeDir;
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasMaskShader(sourceDir)) return sourceDir;
#endif
    return runtimeDir;
}

struct Fixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext processing;

    explicit Fixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        processing.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
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

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

TextureReadback RecordReadbackTexture2D(D3D12Core& core, D3D12CommandContext& commandContext, D3D12Resource& texture) {
    const auto desc = texture.GetDesc();
    TextureReadback rb;
    rb.width = static_cast<UINT>(desc.Width);
    rb.height = desc.Height;
    core.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &rb.layout, &rb.numRows, &rb.rowSize, &rb.totalBytes);
    rb.buffer.Initialize(core.GetDevice(), rb.totalBytes);

    const auto before = texture.GetState();
    if (before != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        commandContext.ResourceBarrier(MakeTransitionBarrier(texture.Get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE));
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

std::vector<uint8_t> ReadbackCompactRgba(TextureReadback& rb) {
    std::vector<uint8_t> out(static_cast<size_t>(rb.width) * rb.height * 4u);
    const auto* mapped = static_cast<const uint8_t*>(rb.buffer.Map());
    const auto* base = mapped + rb.layout.Offset;
    for (UINT y = 0; y < rb.height; ++y) {
        std::memcpy(
            out.data() + static_cast<size_t>(y) * rb.width * 4u,
            base + static_cast<size_t>(y) * rb.layout.Footprint.RowPitch,
            static_cast<size_t>(rb.width) * 4u);
    }
    rb.buffer.Unmap();
    return out;
}

void RequireByteNear(uint8_t actual, uint8_t expected, uint8_t tolerance, const char* label, size_t index) {
    const int diff = actual > expected ? actual - expected : expected - actual;
    if (diff > tolerance) {
        std::ostringstream os;
        os << label << ": byte mismatch at index " << index
           << " actual=" << int(actual)
           << " expected=" << int(expected)
           << " tolerance=" << int(tolerance);
        TEST_FAIL(os.str());
    }
}

} // namespace

TEST(ProcessingMask, ShaderCompile) {
    REQUIRE_CORE(core);
    Fixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.processing);

    CHECK(!cache.GetComputeShader("MaskApplyRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("MaskBlendRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("MaskCombineRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("MaskInvertRgba.hlsl").Empty());
}

TEST(ProcessingMask, ApplyReplaceAlphaReadback) {
    REQUIRE_CORE(core);
    Fixture fx(core);
    if (!fx.processing.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        10, 20, 30, 200,  40, 50, 60, 210,
        70, 80, 90, 220,  100, 110, 120, 230,
    };
    const std::vector<uint8_t> maskPixels = {
        0, 0, 0, 64,    0, 0, 0, 128,
        0, 0, 0, 192,   0, 0, 0, 255,
    };

    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto mask = CreateTexture2DFromRGBA(*core, maskPixels.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12MaskProcessor processor;
    processor.Initialize(fx.processing);
    auto dst = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    MaskApplyDesc desc = {};
    desc.mode = MaskApplyMode::ReplaceAlpha;
    desc.channel = MaskChannel::Alpha;

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    processor.RecordApplyMask(ctx, src, mask, dst, desc);
    auto rb = RecordReadbackTexture2D(*core, ctx, dst);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackCompactRgba(rb);
    for (size_t i = 0; i < got.size(); i += 4) {
        CHECK_EQ(got[i + 0], srcPixels[i + 0]);
        CHECK_EQ(got[i + 1], srcPixels[i + 1]);
        CHECK_EQ(got[i + 2], srcPixels[i + 2]);
        RequireByteNear(got[i + 3], maskPixels[i + 3], 1, "replace alpha", i + 3);
    }
}

TEST(ProcessingMask, BlendCombineAndInvertReadback) {
    REQUIRE_CORE(core);
    Fixture fx(core);
    if (!fx.processing.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> black(4u * 4u, 0);
    std::vector<uint8_t> white(4u * 4u, 255);
    std::vector<uint8_t> halfMask(4u * 4u, 0);
    for (size_t i = 0; i < halfMask.size(); i += 4) halfMask[i + 3] = 128;

    auto base = CreateTexture2DFromRGBA(*core, black.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto overlay = CreateTexture2DFromRGBA(*core, white.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto mask = CreateTexture2DFromRGBA(*core, halfMask.data(), 2, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12MaskProcessor processor;
    processor.Initialize(fx.processing);
    auto blended = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    MaskBlendDesc blendDesc = {};
    blendDesc.channel = MaskChannel::Alpha;
    blendDesc.opacity = 1.0f;

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    processor.RecordBlendByMask(ctx, base, overlay, mask, blended, blendDesc);
    auto rb = RecordReadbackTexture2D(*core, ctx, blended);
    ExecuteAndWait(*core, ctx);

    const auto got = ReadbackCompactRgba(rb);
    for (size_t i = 0; i < got.size(); i += 4) {
        RequireByteNear(got[i + 0], 128, 2, "blend r", i + 0);
        RequireByteNear(got[i + 1], 128, 2, "blend g", i + 1);
        RequireByteNear(got[i + 2], 128, 2, "blend b", i + 2);
        RequireByteNear(got[i + 3], 128, 2, "blend a", i + 3);
    }
}
