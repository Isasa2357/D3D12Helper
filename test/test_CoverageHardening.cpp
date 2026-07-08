#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12Debug.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(runtimeDir / "MaskApplyRgba.hlsl")) return runtimeDir;
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "MaskApplyRgba.hlsl")) return sourceDir;
#endif
    return runtimeDir;
}

struct Fixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext processing;

    explicit Fixture(std::shared_ptr<D3D12Core> c, std::filesystem::path shaderDir = {}) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        processing.Initialize(*core, &cbvSrvUav, &sampler, shaderDir.empty() ? ProcessingShaderDir() : std::move(shaderDir));
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

void RequireBytesEqual(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, const char* label) {
    if (actual.size() != expected.size()) {
        TEST_FAIL(std::string(label) + ": size mismatch");
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::ostringstream os;
            os << label << ": byte mismatch at index " << i
               << " actual=" << int(actual[i])
               << " expected=" << int(expected[i]);
            TEST_FAIL(os.str());
        }
    }
}

} // namespace

TEST(CoverageHardening, ProcessingShaderCacheErrorPaths) {
    D3D12ProcessingShaderCache uninitialized;
    CHECK_THROWS(uninitialized.GetComputeShader("missing.hlsl"));

    REQUIRE_CORE(core);
    Fixture fx(core);
    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.processing);
    CHECK_THROWS(cache.GetComputeShader("ThisShaderDoesNotExist.hlsl"));

    const auto tempDir = std::filesystem::temp_directory_path() / "D3D12Helper_BadProcessingShader";
    std::filesystem::create_directories(tempDir);
    const auto badShader = tempDir / "BadShader.hlsl";
    {
        std::ofstream os(badShader, std::ios::binary);
        os << "this is not valid hlsl";
    }

    Fixture badFx(core, tempDir);
    D3D12ProcessingShaderCache badCache;
    badCache.Initialize(badFx.processing);
    CHECK_THROWS(badCache.GetComputeShader("BadShader.hlsl"));
    badCache.Clear();
    std::filesystem::remove_all(tempDir);
}

TEST(CoverageHardening, D3D12DebugAndInfoQueueErrorPaths) {
    D3D12InfoQueue unattached;
    CHECK(!unattached.IsAvailable());
    CHECK(!unattached.GetStats().available);
    CHECK_THROWS(unattached.Clear());
    CHECK_THROWS(unattached.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false));

    D3D12Debug::SetupInfoQueue(nullptr, false, false, false);
    D3D12Debug::PrintDredInfo(nullptr);
    D3D12Debug::EnableDred();

    REQUIRE_CORE(core);
    D3D12Debug::SetupInfoQueue(core->GetDevice(), false, false, false);
    D3D12Debug::PrintDredInfo(core->GetDevice());
}

TEST(CoverageHardening, CopyResolveBoundaryValidation) {
    REQUIRE_CORE(core);
    auto ctx = core->CreateDirectContext();
    ctx.Reset();

    D3D12Resource nullResource;
    auto bufferA = CreateBuffer(*core, 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto bufferB = CreateBuffer(*core, 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto texA = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto texB = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);

    CHECK_THROWS(RecordCopyResource(ctx, nullResource, texA));
    CHECK_THROWS(RecordCopyBufferRegion(ctx, bufferB, 0, bufferA, 0, 0));
    CHECK_THROWS(RecordCopyBufferRegion(ctx, bufferB, 15, bufferA, 0, 2));
    CHECK_THROWS(RecordCopyBufferRegion(ctx, texA, 0, bufferA, 0, 1));

    D3D12_BOX badDepth = { 0, 0, 0, 1, 1, 2 };
    D3D12_BOX badBounds = { 0, 0, 0, 8, 1, 1 };
    CHECK_THROWS(RecordCopyTextureRegion(ctx, texB, 0, 0, 0, texA, 0, badDepth));
    CHECK_THROWS(RecordCopyTextureRegion(ctx, texB, 0, 0, 0, texA, 0, badBounds));

    CHECK(!IsMultisampledTexture(nullResource));
    CHECK_THROWS(RecordResolveSubresource(ctx, nullResource, 0, texA, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, texB, 0, bufferA, 0));
    CHECK_THROWS(RecordResolveSubresource(ctx, texB, 0, texA, 0));
}

TEST(CoverageHardening, MaskApplyModeGoldenCases) {
    REQUIRE_CORE(core);
    Fixture fx(core);
    if (!fx.processing.SupportsRgba8Uav()) TEST_SKIP("R8G8B8A8 UAV typed store is not supported");

    const std::vector<uint8_t> srcPixels = { 100, 150, 200, 128 };
    const std::vector<uint8_t> maskPixels = { 0, 0, 0, 128 };
    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 1, 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto mask = CreateTexture2DFromRGBA(*core, maskPixels.data(), 1, 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12MaskProcessor processor;
    processor.Initialize(fx.processing);

    auto run = [&](MaskApplyMode mode) {
        auto dst = processor.CreateOutputTexture(*core, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
        MaskApplyDesc desc = {};
        desc.mode = mode;
        desc.channel = MaskChannel::Alpha;
        desc.strength = 1.0f;
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordApplyMask(commandContext, src, mask, dst, desc);
        auto rb = RecordReadbackTexture2D(*core, commandContext, dst);
        ExecuteAndWait(*core, commandContext);
        return ReadbackCompactRgba(rb);
    };

    const auto multiplyRgb = run(MaskApplyMode::MultiplyRgb);
    RequireByteNear(multiplyRgb[0], 50, 2, "multiply rgb r", 0);
    RequireByteNear(multiplyRgb[1], 75, 2, "multiply rgb g", 1);
    RequireByteNear(multiplyRgb[2], 100, 2, "multiply rgb b", 2);
    RequireByteNear(multiplyRgb[3], 128, 1, "multiply rgb a", 3);

    const auto multiplyRgba = run(MaskApplyMode::MultiplyRgba);
    RequireByteNear(multiplyRgba[0], 50, 2, "multiply rgba r", 0);
    RequireByteNear(multiplyRgba[1], 75, 2, "multiply rgba g", 1);
    RequireByteNear(multiplyRgba[2], 100, 2, "multiply rgba b", 2);
    RequireByteNear(multiplyRgba[3], 64, 2, "multiply rgba a", 3);
}

TEST(CoverageHardening, ThresholdInvertGoldenCase) {
    REQUIRE_CORE(core);
    Fixture fx(core);
    if (!fx.processing.SupportsRgba8Uav()) TEST_SKIP("R8G8B8A8 UAV typed store is not supported");

    const std::vector<uint8_t> srcPixels = {
        0,   0, 0, 255,
        255, 0, 0, 255,
    };
    auto src = CreateTexture2DFromRGBA(*core, srcPixels.data(), 2, 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor processor;
    processor.Initialize(fx.processing);
    auto dst = processor.CreateOutputTexture(*core, 2, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

    ThresholdDesc desc = {};
    desc.channel = MaskChannel::Red;
    desc.threshold = 0.5f;
    desc.invert = true;
    desc.foregroundColor[0] = 1.0f;
    desc.foregroundColor[1] = 1.0f;
    desc.foregroundColor[2] = 1.0f;
    desc.foregroundColor[3] = 1.0f;
    desc.backgroundColor[0] = 0.0f;
    desc.backgroundColor[1] = 0.0f;
    desc.backgroundColor[2] = 0.0f;
    desc.backgroundColor[3] = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordThreshold(commandContext, src, dst, desc);
    auto rb = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const std::vector<uint8_t> expected = {
        255, 255, 255, 255,
        0,   0,   0,   255,
    };
    RequireBytesEqual(ReadbackCompactRgba(rb), expected, "threshold invert");
}
