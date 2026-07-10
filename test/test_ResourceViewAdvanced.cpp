//
// test_ResourceViewAdvanced.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceValidation.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(
        dir / "AdvancedTransformRgba.hlsl", ec) && !ec &&
        std::filesystem::exists(dir / "ApplyLut3D.hlsl", ec) && !ec;
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

D3D12_HEAP_PROPERTIES DefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12Resource CreateTexture3D(
    D3D12Core& core,
    UINT width,
    UINT height,
    UINT16 depth,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    const auto heap = DefaultHeapProperties();
    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    return D3D12Resource(std::move(resource), initialState);
}

ULONG ProbeReferenceCount(IUnknown* object) {
    if (!object) {
        TEST_FAIL("ProbeReferenceCount: null object");
    }
    object->AddRef();
    return object->Release();
}

std::vector<uint8_t> MakeRgba2x2() {
    return {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 0, 255,
    };
}

} // namespace

TEST(ResourceView, AdvancedViewRequiresExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba2x2();
    auto src = CreateTexture2DFromMemory(
        *core,
        pixels.data(),
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(
        *core,
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    AffineTransformDesc desc;
    desc.filter = ProcessingFilter::Point;
    desc.dstRect = { 0, 0, 2, 2 };

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    CHECK_THROWS(processor.RecordAffineTransformView(
        commandContext,
        D3D12ResourceView(src),
        D3D12ResourceView(dst),
        desc,
        D3D12ProcessingStateDesc{}));
    commandContext.Close();
}

TEST(ResourceView, AdvancedViewsRecordWithExplicitStates) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba2x2();
    auto src = CreateTexture2DFromMemory(
        *core,
        pixels.data(),
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        8,
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

    auto lut = CreateTexture3D(
        *core,
        2,
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);

    const auto oneInputState = OneInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);
    const auto twoInputState = TwoInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    auto affineDst = processor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    AffineTransformDesc affine;
    affine.filter = ProcessingFilter::Point;
    affine.dstRect = { 0, 0, 2, 2 };
    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordAffineTransformView(
            commandContext,
            D3D12ResourceView(src),
            D3D12ResourceView(affineDst),
            affine,
            oneInputState);
        ExecuteAndWait(*core, commandContext);
    }

    auto perspectiveDst = processor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    PerspectiveTransformDesc perspective;
    perspective.filter = ProcessingFilter::Point;
    perspective.dstRect = { 0, 0, 2, 2 };
    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordPerspectiveTransformView(
            commandContext,
            D3D12ResourceView(src),
            D3D12ResourceView(perspectiveDst),
            perspective,
            oneInputState);
        ExecuteAndWait(*core, commandContext);
    }

    auto lutDst = processor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    Lut3DDesc lutDesc;
    lutDesc.dstRect = { 0, 0, 2, 2 };
    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordApplyLut3DView(
            commandContext,
            D3D12ResourceView(src),
            D3D12ResourceView(lut),
            D3D12ResourceView(lutDst),
            lutDesc,
            twoInputState);
        ExecuteAndWait(*core, commandContext);
    }

    auto undistortDst = processor.CreateOutputTexture(
        *core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    RemapDesc remap;
    remap.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    remap.mapFormat = DXGI_FORMAT_R32G32_FLOAT;
    remap.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    remap.filter = ProcessingFilter::Point;
    remap.coordinateMode = RemapCoordinateMode::AbsolutePixels;
    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordApplyUndistortMapView(
            commandContext,
            D3D12ResourceView(src),
            D3D12ResourceView(map),
            D3D12ResourceView(undistortDst),
            remap,
            twoInputState);
        ExecuteAndWait(*core, commandContext);
    }

    CHECK(src.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(map.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(lut.GetState() == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(affineDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(perspectiveDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(lutDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
    CHECK(undistortDst.GetState() == D3D12_RESOURCE_STATE_COMMON);
}

TEST(ResourceView, ViewAndAdvancedProcessingDoNotRetainComReferences) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto pixels = MakeRgba2x2();
    auto src = CreateTexture2DFromMemory(
        *core,
        pixels.data(),
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto lut = CreateTexture3D(
        *core,
        2,
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(
        *core,
        2,
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    const ULONG srcRefsBefore = ProbeReferenceCount(src.Get());
    const ULONG lutRefsBefore = ProbeReferenceCount(lut.Get());
    const ULONG dstRefsBefore = ProbeReferenceCount(dst.Get());

    {
        D3D12ResourceView srcView(src);
        D3D12ResourceView dstView(dst);

        D3D12Texture2DRequirement requirement;
        requirement.width = 2;
        requirement.height = 2;
        requirement.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        requirement.expectedDevice = core->GetDevice();
        CHECK(ValidateTexture2DView(srcView, requirement).IsValid());

        D3D12BarrierBatch barriers;
        CHECK(barriers.Transition(
            srcView.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        CHECK_EQ(barriers.Count(), 1u);

        CHECK_EQ(ProbeReferenceCount(src.Get()), srcRefsBefore);
        CHECK_EQ(ProbeReferenceCount(dst.Get()), dstRefsBefore);
    }

    const auto state = TwoInputStates(
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON);

    Lut3DDesc desc;
    desc.dstRect = { 0, 0, 2, 2 };
    {
        auto commandContext = core->CreateDirectContext();
        commandContext.Reset();
        processor.RecordApplyLut3DView(
            commandContext,
            D3D12ResourceView(src),
            D3D12ResourceView(lut),
            D3D12ResourceView(dst),
            desc,
            state);
        ExecuteAndWait(*core, commandContext);
    }

    CHECK_EQ(ProbeReferenceCount(src.Get()), srcRefsBefore);
    CHECK_EQ(ProbeReferenceCount(lut.Get()), lutRefsBefore);
    CHECK_EQ(ProbeReferenceCount(dst.Get()), dstRefsBefore);
}
