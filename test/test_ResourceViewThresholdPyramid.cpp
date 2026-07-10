//
// test_ResourceViewThresholdPyramid.cpp
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
    return std::filesystem::exists(dir / "ThresholdRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir =
        std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(runtimeDir)) return runtimeDir;

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(sourceDir)) return sourceDir;
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
            1024,
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

std::vector<uint8_t> MakeRgba(UINT width, UINT height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < pixels.size(); i += 4u) {
        pixels[i + 0] = static_cast<uint8_t>(i * 3u + 11u);
        pixels[i + 1] = static_cast<uint8_t>(i * 5u + 29u);
        pixels[i + 2] = static_cast<uint8_t>(i * 7u + 47u);
        pixels[i + 3] = 255u;
    }
    return pixels;
}

D3D12ProcessingStateDesc OneInputState(
    D3D12_RESOURCE_STATES srcBefore,
    D3D12_RESOURCE_STATES srcAfter,
    D3D12_RESOURCE_STATES dstBefore,
    D3D12_RESOURCE_STATES dstAfter) {

    D3D12ProcessingStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = srcBefore;
    state.srcAfter = srcAfter;
    state.dstBefore = dstBefore;
    state.dstAfter = dstAfter;
    return state;
}

D3D12PyramidBlurWorkspaceStateDesc UniformWorkspaceState(
    const D3D12PyramidBlurWorkspaceView& workspace,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {

    D3D12PyramidBlurWorkspaceStateDesc state;
    state.downBefore.assign(workspace.downTextures.size(), before);
    state.downAfter.assign(workspace.downTextures.size(), after);
    state.blurScratchBefore = before;
    state.blurScratchAfter = after;
    state.blurredLowBefore = before;
    state.blurredLowAfter = after;
    state.upBefore.assign(workspace.upTextures.size(), before);
    state.upAfter.assign(workspace.upTextures.size(), after);
    return state;
}

} // namespace

TEST(ResourceView, ThresholdViewVariantsRecordWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT width = 8;
    const UINT height = 8;
    const auto pixels = MakeRgba(width, height);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), width, height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor processor;
    processor.Initialize(fx.context);

    auto thresholdDst = processor.CreateOutputTexture(
        *core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto rangeDst = processor.CreateOutputTexture(
        *core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto heatmapDst = processor.CreateOutputTexture(
        *core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto classDst = processor.CreateOutputTexture(
        *core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto overlayDst = processor.CreateOutputTexture(
        *core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    const auto state = OneInputState(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();

    processor.RecordThresholdView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(thresholdDst), ThresholdDesc{}, state);
    processor.RecordRangeThresholdView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(rangeDst), RangeThresholdDesc{}, state);
    processor.RecordConfidenceHeatmapView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(heatmapDst), ConfidenceHeatmapDesc{}, state);
    processor.RecordClassColorMapView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(classDst), ClassColorMapDesc{}, state);
    processor.RecordMaskOverlayView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(overlayDst), MaskOverlayDesc{}, state);

    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(thresholdDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(rangeDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(heatmapDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(classDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(overlayDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, ThresholdAndPyramidViewsRequireExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(4, 4);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12ThresholdProcessor threshold;
    threshold.Initialize(fx.context);
    auto thresholdDst = threshold.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D12PyramidProcessor pyramid;
    pyramid.Initialize(fx.context);
    auto down = pyramid.CreateDownsampledTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();

    CHECK_THROWS(threshold.RecordThresholdView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(thresholdDst), ThresholdDesc{}, {}));
    CHECK_THROWS(pyramid.RecordDownsample2xView(
        commandContext, D3D12ResourceView(src),
        D3D12ResourceView(down), PyramidDownsampleDesc{}, {}));

    commandContext.Close();
}

TEST(ResourceView, PyramidPrimitiveViewsRecordWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(8, 8);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 8, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidProcessor pyramid;
    pyramid.Initialize(fx.context);
    auto down = pyramid.CreateDownsampledTexture(
        *core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto up = pyramid.CreateUpsampledTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();

    PyramidDownsampleDesc downDesc;
    pyramid.RecordDownsample2xView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(down),
        downDesc,
        OneInputState(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    PyramidUpsampleDesc upDesc;
    pyramid.RecordUpsample2xView(
        commandContext,
        D3D12ResourceView(down),
        D3D12ResourceView(up),
        upDesc,
        OneInputState(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COMMON));

    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(down.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(up.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, PyramidBlurViewRecordsWithWorkspaceStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(8, 8);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 8, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidBlur blur;
    blur.Initialize(fx.context);
    auto workspace = blur.CreateWorkspace(
        *core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 1);
    auto dst = blur.CreateOutputTexture(
        *core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12PyramidBlurWorkspaceView workspaceView(workspace);

    D3D12PyramidBlurStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    state.workspace = UniformWorkspaceState(
        workspaceView,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    PyramidBlurDesc desc;
    desc.levels = 1;
    desc.blurRadius = 1;
    desc.blurSigma = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    blur.RecordPyramidBlurView(
        commandContext,
        D3D12ResourceView(src),
        workspaceView,
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(workspace.downTextures[0].GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(workspace.blurScratch.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(workspace.blurredLow.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, PyramidRegionBlurViewRecordsWithWorkspaceStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(8, 8);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 8, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12PyramidRegionBlur blur;
    blur.Initialize(fx.context);
    auto workspace = blur.CreateWorkspace(
        *core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 1);
    auto dst = blur.CreateOutputTexture(
        *core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12PyramidRegionBlurWorkspaceView workspaceView(workspace);

    D3D12PyramidRegionBlurStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    state.blurredBefore = D3D12_RESOURCE_STATE_COMMON;
    state.blurredAfter = D3D12_RESOURCE_STATE_COMMON;
    state.blurWorkspace = UniformWorkspaceState(
        workspaceView.blurWorkspace,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    PyramidRegionBlurDesc desc;
    desc.levels = 1;
    desc.blurRadius = 1;
    desc.blurSigma = 1.0f;
    desc.shape = RegionShape::Circle;
    desc.radius = 2.0f;
    desc.centerX = 4.0f;
    desc.centerY = 4.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    blur.RecordPyramidRegionBlurView(
        commandContext,
        D3D12ResourceView(src),
        workspaceView,
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(workspace.blurred.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(workspace.blurWorkspace.downTextures[0].GetState() == D3D12_RESOURCE_STATE_COMMON);
}
