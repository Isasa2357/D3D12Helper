//
// test_ResourceView.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceValidation.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "ConvertRgbToRgb.hlsl", ec) && !ec;
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

D3D12ProcessingStateDesc MakeExplicitStates(
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

} // namespace

static_assert(std::is_trivially_copyable_v<D3D12ResourceView>);
static_assert(std::is_trivially_destructible_v<D3D12ResourceView>);
static_assert(sizeof(D3D12ResourceView) == sizeof(ID3D12Resource*));

TEST(ResourceView, IsNonOwningPointerSizedView) {
    D3D12ResourceView empty;
    CHECK(!empty);
    CHECK(empty.Get() == nullptr);
    CHECK(empty.GetWidth() == 0);
    CHECK(empty.GetHeight() == 0);
    CHECK(empty.GetFormat() == DXGI_FORMAT_UNKNOWN);

    auto* fake = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1000));
    D3D12ResourceView view(fake);
    CHECK(view);
    CHECK(view.Get() == fake);

    D3D12ResourceView copied = view;
    CHECK(copied.Get() == fake);
}

TEST(ResourceView, WrapsOwnedResourceWithoutChangingOwnershipAPI) {
    REQUIRE_CORE(core);

    D3D12Resource resource = CreateTexture2D(
        *core,
        32,
        16,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12ResourceView view(resource);
    CHECK(view.Get() == resource.Get());
    CHECK_EQ(view.GetWidth(), 32ull);
    CHECK_EQ(view.GetHeight(), 16u);
    CHECK(view.GetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D12Texture2DRequirement requirement;
    requirement.width = 32;
    requirement.height = 16;
    requirement.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    requirement.requiredFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    requirement.expectedDevice = core->GetDevice();

    const auto result = ValidateTexture2DView(view, requirement);
    CHECK(result.IsValid());
    CHECK_NOTHROW(ValidateTexture2DViewOrThrow(view, requirement));

    D3D12BarrierBatch barriers;
    CHECK(barriers.Transition(
        view.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_SOURCE));
    barriers.Uav(view.Get());
    CHECK_EQ(barriers.Count(), 2u);
}

TEST(ResourceView, ProcessingViewRequiresExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> pixels(4u * 4u * 4u, 127u);
    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
        4,
        4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(
        *core,
        4,
        4,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    FormatConvertDesc desc;
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();

    CHECK_THROWS(converter.RecordConvertView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        D3D12ProcessingStateDesc{}));

    commandContext.Close();
}

TEST(ResourceView, FormatConverterRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(
        *core,
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    FormatConvertDesc desc;
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    const auto state = MakeExplicitStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    converter.RecordConvertView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    // The view API never mutates the owned wrapper's independent state cache.
    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, FusedProcessorRecordsWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 0, 255,
    };
    auto src = CreateTexture2DFromRGBA(
        *core,
        pixels.data(),
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

    FusedConvertResizeDesc desc;
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.filter = ProcessingFilter::Point;

    const auto state = MakeExplicitStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    fused.RecordConvertResizeView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        state);
    ExecuteAndWait(*core, commandContext);

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(dst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}
