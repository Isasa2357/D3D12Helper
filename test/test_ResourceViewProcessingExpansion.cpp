//
// test_ResourceViewProcessingExpansion.cpp
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
    return std::filesystem::exists(dir / "ResizeRgba.hlsl", ec) && !ec;
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
            256,
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

D3D12ProcessingStateDesc OneInputStates(
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

D3D12ProcessingTwoInputStateDesc TwoInputStates(
    D3D12_RESOURCE_STATES src0Before,
    D3D12_RESOURCE_STATES src0After,
    D3D12_RESOURCE_STATES src1Before,
    D3D12_RESOURCE_STATES src1After,
    D3D12_RESOURCE_STATES dstBefore,
    D3D12_RESOURCE_STATES dstAfter) {

    D3D12ProcessingTwoInputStateDesc state;
    state.useExplicitStates = true;
    state.src0Before = src0Before;
    state.src0After = src0After;
    state.src1Before = src1Before;
    state.src1After = src1After;
    state.dstBefore = dstBefore;
    state.dstAfter = dstAfter;
    return state;
}

std::vector<uint8_t> MakeRgba(UINT width, UINT height, uint8_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < pixels.size(); i += 4u) {
        pixels[i + 0] = static_cast<uint8_t>(seed + i);
        pixels[i + 1] = static_cast<uint8_t>(seed + i + 17u);
        pixels[i + 2] = static_cast<uint8_t>(seed + i + 31u);
        pixels[i + 3] = 255u;
    }
    return pixels;
}

} // namespace

TEST(ResourceView, ResizerViewRequiresExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(2, 2, 3);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Resizer resizer;
    resizer.Initialize(fx.context);
    auto dst = resizer.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(resizer.RecordResizeView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        ResizeDesc{},
        D3D12ProcessingStateDesc{}));
    commandContext.Close();
}

TEST(ResourceView, ResizerViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(2, 2, 7);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Resizer resizer;
    resizer.Initialize(fx.context);
    auto dst = resizer.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    ResizeDesc desc;
    desc.filter = ProcessingFilter::Point;
    const auto state = OneInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    resizer.RecordResizeView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, RemapperViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(2, 2, 11);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const std::vector<float> mapValues = {
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 1.0f, 1.0f,
    };
    auto map = CreateTexture2DFromMemory(
        *core,
        mapValues.data(),
        2,
        2,
        DXGI_FORMAT_R32G32_FLOAT,
        2u * static_cast<UINT>(sizeof(float)),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Remapper remapper;
    remapper.Initialize(fx.context);
    auto dst = remapper.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    RemapDesc desc;
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.mapFormat = DXGI_FORMAT_R32G32_FLOAT;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.filter = ProcessingFilter::Point;
    desc.coordinateMode = RemapCoordinateMode::AbsolutePixels;

    const auto state = TwoInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    remapper.RecordRemapView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(map),
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(map.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, CompositorViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto basePixels = MakeRgba(2, 2, 19);
    const auto overlayPixels = MakeRgba(2, 2, 73);
    auto base = CreateTexture2DFromRGBA(
        *core, basePixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto overlay = CreateTexture2DFromRGBA(
        *core, overlayPixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Compositor compositor;
    compositor.Initialize(fx.context);
    auto dst = compositor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    CompositeDesc desc;
    desc.baseFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.overlayFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.blendMode = CompositeBlendMode::AlphaBlend;
    desc.opacity = 0.5f;

    const auto state = TwoInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    compositor.RecordCompositeView(
        commandContext,
        D3D12ResourceView(base),
        D3D12ResourceView(overlay),
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(base.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(overlay.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, AliasedTwoInputViewsRequireMatchingStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(2, 2, 101);
    auto sharedInput = CreateTexture2DFromRGBA(
        *core, pixels.data(), 2, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Compositor compositor;
    compositor.Initialize(fx.context);
    auto dst = compositor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    CompositeDesc desc;
    desc.baseFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.overlayFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto state = TwoInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(compositor.RecordCompositeView(
        commandContext,
        D3D12ResourceView(sharedInput),
        D3D12ResourceView(sharedInput),
        D3D12ResourceView(dst),
        desc,
        state));
    commandContext.Close();
}
