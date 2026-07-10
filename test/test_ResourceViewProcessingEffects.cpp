//
// test_ResourceViewProcessingEffects.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <filesystem>
#include <memory>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "ColorAdjustRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir =
        std::filesystem::current_path() / "shaders" / "D3D12Processing";
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

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c)
        : core(std::move(c)) {
        cbvSrvUav.Initialize(
            core->GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            128,
            true);
        sampler.Initialize(
            core->GetDevice(),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
            8,
            true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

D3D12ProcessingStateDesc ExplicitStates() {
    D3D12ProcessingStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

std::vector<uint8_t> MakePixels(UINT width, UINT height, uint8_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < pixels.size(); i += 4u) {
        pixels[i + 0] = static_cast<uint8_t>(seed + i);
        pixels[i + 1] = static_cast<uint8_t>(seed + i + 23u);
        pixels[i + 2] = static_cast<uint8_t>(seed + i + 47u);
        pixels[i + 3] = 255u;
    }
    return pixels;
}

D3D12Resource CreateOutput(D3D12Core& core, UINT width, UINT height) {
    return CreateTexture2D(
        core,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

} // namespace

TEST(ResourceView, ColorAdjustViewRequiresExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakePixels(4, 4, 5);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto dst = CreateOutput(*core, 4, 4);

    D3D12ColorAdjuster adjuster;
    adjuster.Initialize(fx.context);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(adjuster.RecordColorAdjustView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        ColorAdjustDesc{},
        D3D12ProcessingStateDesc{}));
    commandContext.Close();
}

TEST(ResourceView, ColorAdjustViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakePixels(4, 4, 11);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto dst = CreateOutput(*core, 4, 4);

    ColorAdjustDesc desc;
    desc.brightness = 0.05f;
    desc.contrast = 1.1f;
    desc.gamma = 1.0f;
    desc.saturation = 0.9f;

    D3D12ColorAdjuster adjuster;
    adjuster.Initialize(fx.context);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    adjuster.RecordColorAdjustView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        ExplicitStates());
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, KernelFilterViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakePixels(4, 4, 29);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto dst = CreateOutput(*core, 4, 4);

    KernelFilterDesc desc;
    desc.mode = KernelFilterMode::Sharpen;
    desc.edgeMode = KernelEdgeMode::Clamp;

    D3D12KernelFilter filter;
    filter.Initialize(fx.context);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    filter.RecordKernelFilterView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        ExplicitStates());
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, RegionEffectViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakePixels(4, 4, 53);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto dst = CreateOutput(*core, 4, 4);

    RegionEffectDesc desc;
    desc.shape = RegionShape::Circle;
    desc.selection = RegionSelection::Outside;
    desc.effect = RegionEffectMode::Darken;
    desc.centerX = 2.0f;
    desc.centerY = 2.0f;
    desc.radius = 1.25f;
    desc.edgeSoftness = 0.25f;
    desc.strength = 0.5f;

    D3D12RegionEffectProcessor processor;
    processor.Initialize(fx.context);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordRegionEffectView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        ExplicitStates());
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}
