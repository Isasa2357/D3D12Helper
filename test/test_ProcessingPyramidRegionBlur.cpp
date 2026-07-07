#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "RegionBlurBlendRgba.hlsl", ec) && !ec;
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
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& ctx) {
    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

std::vector<uint8_t> MakeInput(UINT width, UINT height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            pixels[i + 0] = static_cast<uint8_t>((x * 255u) / (width - 1u));
            pixels[i + 1] = static_cast<uint8_t>((y * 255u) / (height - 1u));
            pixels[i + 2] = static_cast<uint8_t>(((x ^ y) * 7u) & 0xFFu);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

PyramidRegionBlurDesc MakeValidDesc(UINT width, UINT height) {
    PyramidRegionBlurDesc desc = {};
    desc.levels = 2;
    desc.shape = RegionShape::Circle;
    desc.selection = RegionSelection::Outside;
    desc.centerX = static_cast<float>(width) * 0.5f;
    desc.centerY = static_cast<float>(height) * 0.5f;
    desc.radius = static_cast<float>(width) * 0.25f;
    desc.edgeSoftness = 4.0f;
    desc.blurStrength = 1.0f;
    desc.blurMode = BlurMode::Gaussian;
    desc.blurRadius = 2;
    desc.blurSigma = 1.25f;
    desc.upsampleFilter = ProcessingFilter::Linear;
    return desc;
}

} // namespace

TEST(ProcessingPyramidRegionBlur, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);

    CHECK(!cache.GetComputeShader("PyramidDownsample2xRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("PyramidUpsample2xRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("GaussianBlurHorizontalRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("GaussianBlurVerticalRgba.hlsl").Empty());
    CHECK(!cache.GetComputeShader("RegionBlurBlendRgba.hlsl").Empty());
}

TEST(ProcessingPyramidRegionBlur, CircleOutsideRuns) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    constexpr UINT width = 32;
    constexpr UINT height = 32;
    const auto pixels = MakeInput(width, height);

    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidRegionBlur regionBlur;
    regionBlur.Initialize(fx.context);
    auto workspace = regionBlur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    auto dst = regionBlur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto desc = MakeValidDesc(width, height);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    regionBlur.RecordPyramidRegionBlur(ctx, src, workspace, dst, desc);
    ExecuteAndWait(*core, ctx);

    CHECK(dst.Get() != nullptr);
    CHECK_EQ(dst.GetWidth(), static_cast<UINT64>(width));
    CHECK_EQ(dst.GetHeight(), height);
}

TEST(ProcessingPyramidRegionBlur, RejectsInvalidCircleRadius) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    constexpr UINT width = 32;
    constexpr UINT height = 32;
    const auto pixels = MakeInput(width, height);

    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidRegionBlur regionBlur;
    regionBlur.Initialize(fx.context);
    auto workspace = regionBlur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    auto dst = regionBlur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto desc = MakeValidDesc(width, height);
    desc.radius = 0.0f;

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_THROWS(regionBlur.RecordPyramidRegionBlur(ctx, src, workspace, dst, desc));
}
