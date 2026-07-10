//
// test_ResourceViewBlurMask.cpp
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
    return std::filesystem::exists(dir / "GaussianBlurHorizontalRgba.hlsl", ec) &&
           std::filesystem::exists(dir / "MaskApplyRgba.hlsl", ec) && !ec;
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
            512,
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

std::vector<uint8_t> MakeRgba(UINT width, UINT height, uint8_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (size_t i = 0; i < pixels.size(); i += 4u) {
        pixels[i + 0] = static_cast<uint8_t>(seed + i);
        pixels[i + 1] = static_cast<uint8_t>(seed + i + 19u);
        pixels[i + 2] = static_cast<uint8_t>(seed + i + 37u);
        pixels[i + 3] = static_cast<uint8_t>(64u + ((i / 4u) % 4u) * 48u);
    }
    return pixels;
}

D3D12ProcessingStateDesc OneInputStates() {
    D3D12ProcessingStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

D3D12ProcessingTwoInputStateDesc TwoInputStates() {
    D3D12ProcessingTwoInputStateDesc state;
    state.useExplicitStates = true;
    state.src0Before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src0After = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src1Before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src1After = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

D3D12ProcessingThreeInputStateDesc ThreeInputStates() {
    D3D12ProcessingThreeInputStateDesc state;
    state.useExplicitStates = true;
    state.src0Before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src0After = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src1Before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src1After = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src2Before = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.src2After = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

D3D12ProcessingBlurStateDesc BlurStates() {
    D3D12ProcessingBlurStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.scratchBefore = D3D12_RESOURCE_STATE_COMMON;
    state.scratchAfter = D3D12_RESOURCE_STATE_COMMON;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

D3D12ProcessingRegionBlurStateDesc RegionBlurStates() {
    D3D12ProcessingRegionBlurStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.blurScratchBefore = D3D12_RESOURCE_STATE_COMMON;
    state.blurScratchAfter = D3D12_RESOURCE_STATE_COMMON;
    state.blurredBefore = D3D12_RESOURCE_STATE_COMMON;
    state.blurredAfter = D3D12_RESOURCE_STATE_COMMON;
    state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
    state.dstAfter = D3D12_RESOURCE_STATE_COMMON;
    return state;
}

} // namespace

TEST(ResourceView, BlurViewRequiresExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(4, 4, 3);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Blurrer blurrer;
    blurrer.Initialize(fx.context);
    auto scratch = blurrer.CreateScratchTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = blurrer.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(blurrer.RecordBlurView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(scratch),
        D3D12ResourceView(dst),
        BlurDesc{},
        D3D12ProcessingBlurStateDesc{}));
    commandContext.Close();
}

TEST(ResourceView, BlurrerViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(4, 4, 7);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Blurrer blurrer;
    blurrer.Initialize(fx.context);
    auto scratch = blurrer.CreateScratchTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = blurrer.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    BlurDesc desc;
    desc.mode = BlurMode::Gaussian;
    desc.radius = 1;
    desc.sigma = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    blurrer.RecordBlurView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(scratch),
        D3D12ResourceView(dst),
        desc,
        BlurStates());
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(scratch.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, RegionBlurViewRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(4, 4, 11);
    auto src = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12RegionBlur regionBlur;
    regionBlur.Initialize(fx.context);
    auto scratch = regionBlur.CreateScratchTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto blurred = regionBlur.CreateBlurredTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = regionBlur.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    RegionBlurDesc desc;
    desc.shape = RegionShape::Circle;
    desc.selection = RegionSelection::Outside;
    desc.centerX = 2.0f;
    desc.centerY = 2.0f;
    desc.radius = 1.5f;
    desc.blurRadius = 1;
    desc.blurSigma = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    regionBlur.RecordRegionBlurView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(scratch),
        D3D12ResourceView(blurred),
        D3D12ResourceView(dst),
        desc,
        RegionBlurStates());
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(scratch.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(blurred.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, MaskViewsRecordWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto basePixels = MakeRgba(4, 4, 17);
    const auto overlayPixels = MakeRgba(4, 4, 53);
    const auto maskAPixels = MakeRgba(4, 4, 89);
    const auto maskBPixels = MakeRgba(4, 4, 127);

    auto base = CreateTexture2DFromRGBA(
        *core, basePixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto overlay = CreateTexture2DFromRGBA(
        *core, overlayPixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto maskA = CreateTexture2DFromRGBA(
        *core, maskAPixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto maskB = CreateTexture2DFromRGBA(
        *core, maskBPixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12MaskProcessor processor;
    processor.Initialize(fx.context);

    auto applyDst = processor.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto blendDst = processor.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto combineDst = processor.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto invertDst = processor.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordApplyMaskView(
            commandContext,
            D3D12ResourceView(base),
            D3D12ResourceView(maskA),
            D3D12ResourceView(applyDst),
            MaskApplyDesc{},
            TwoInputStates());
        ExecuteAndWait(*core, commandContext);
    }

    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordBlendByMaskView(
            commandContext,
            D3D12ResourceView(base),
            D3D12ResourceView(overlay),
            D3D12ResourceView(maskA),
            D3D12ResourceView(blendDst),
            MaskBlendDesc{},
            ThreeInputStates());
        ExecuteAndWait(*core, commandContext);
    }

    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordCombineMasksView(
            commandContext,
            D3D12ResourceView(maskA),
            D3D12ResourceView(maskB),
            D3D12ResourceView(combineDst),
            MaskCombineDesc{},
            TwoInputStates());
        ExecuteAndWait(*core, commandContext);
    }

    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordInvertMaskView(
            commandContext,
            D3D12ResourceView(maskA),
            D3D12ResourceView(invertDst),
            MaskInvertDesc{},
            OneInputStates());
        ExecuteAndWait(*core, commandContext);
    }

    CHECK(base.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(overlay.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(maskA.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(maskB.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(applyDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(blendDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(combineDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(invertDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, MaskViewRejectsAliasedInputs) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba(4, 4, 151);
    auto shared = CreateTexture2DFromRGBA(
        *core, pixels.data(), 4, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12MaskProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(
        *core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(processor.RecordApplyMaskView(
        commandContext,
        D3D12ResourceView(shared),
        D3D12ResourceView(shared),
        D3D12ResourceView(dst),
        MaskApplyDesc{},
        TwoInputStates()));
    commandContext.Close();
}
