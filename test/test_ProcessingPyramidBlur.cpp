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
    return std::filesystem::exists(dir / "PyramidDownsample2xRgba.hlsl", ec) && !ec;
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
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true);
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
            pixels[i + 2] = static_cast<uint8_t>((x + y) * 3u);
            pixels[i + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

TEST(ProcessingPyramidBlur, ShaderCompile) {
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

TEST(ProcessingPyramidBlur, PyramidBlurAndRegionBlurRun) {
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

    D3D12PyramidBlur pyramidBlur;
    pyramidBlur.Initialize(fx.context);
    auto blurWorkspace = pyramidBlur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    auto blurDst = pyramidBlur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    PyramidBlurDesc blurDesc = {};
    blurDesc.levels = 2;
    blurDesc.blurMode = BlurMode::Gaussian;
    blurDesc.blurRadius = 2;
    blurDesc.blurSigma = 1.25f;
    blurDesc.upsampleFilter = ProcessingFilter::Linear;

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    pyramidBlur.RecordPyramidBlur(ctx, src, blurWorkspace, blurDst, blurDesc);
    ExecuteAndWait(*core, ctx);

    D3D12PyramidRegionBlur regionBlur;
    regionBlur.Initialize(fx.context);
    auto regionWorkspace = regionBlur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    auto regionDst = regionBlur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    PyramidRegionBlurDesc regionDesc = {};
    regionDesc.levels = 2;
    regionDesc.shape = RegionShape::Circle;
    regionDesc.selection = RegionSelection::Outside;
    regionDesc.centerX = 16.0f;
    regionDesc.centerY = 16.0f;
    regionDesc.radius = 8.0f;
    regionDesc.edgeSoftness = 4.0f;
    regionDesc.blurStrength = 1.0f;
    regionDesc.blurRadius = 2;
    regionDesc.blurSigma = 1.25f;

    ctx = core->CreateDirectContext();
    ctx.Reset();
    regionBlur.RecordPyramidRegionBlur(ctx, src, regionWorkspace, regionDst, regionDesc);
    ExecuteAndWait(*core, ctx);

    CHECK(regionDst.Get() != nullptr);
    CHECK_EQ(regionDst.GetWidth(), static_cast<UINT64>(width));
    CHECK_EQ(regionDst.GetHeight(), height);
}
