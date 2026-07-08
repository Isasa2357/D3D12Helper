#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct ProcessingConstantsForSample {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;

    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;

    UINT srcFormat = 0;
    UINT dstFormat = 0;
    UINT srcMatrix = 0;
    UINT srcRange = 0;

    UINT dstMatrix = 0;
    UINT dstRange = 0;
    UINT filter = 0;
    UINT alphaMode = 0;

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    UINT reserved0 = 0;
    UINT reserved1 = 0;
};
static_assert((sizeof(ProcessingConstantsForSample) % 4) == 0, "root constants must be DWORD aligned");

UINT DivideRoundUp(UINT value, UINT divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

std::filesystem::path ProcessingShaderDir() {
    const auto namespacedRuntimeDir =
        std::filesystem::current_path() / "D3D12Helper" / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(namespacedRuntimeDir / "ProcessingCommon.hlsli")) {
        return namespacedRuntimeDir;
    }

    const auto legacyRuntimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(legacyRuntimeDir / "ProcessingCommon.hlsli")) {
        return legacyRuntimeDir;
    }

#ifdef D3D12HELPER_SAMPLE_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_SAMPLE_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "ProcessingCommon.hlsli")) {
        return sourceDir;
    }
#endif

    return namespacedRuntimeDir;
}

std::filesystem::path SampleShaderPath() {
#ifdef D3D12HELPER_SAMPLE_SOURCE_DIR
    const auto sourcePath = std::filesystem::u8path(D3D12HELPER_SAMPLE_SOURCE_DIR) /
        "18_ProcessingCustomFusedShader" / "CustomNv12ResizeDarken.hlsl";
    if (std::filesystem::exists(sourcePath)) {
        return sourcePath;
    }
#endif

    return std::filesystem::current_path() / "CustomNv12ResizeDarken.hlsl";
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& ctx) {
    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

D3D12Resource CreateUploadedNv12Gradient(D3D12Core& core, UINT width, UINT height) {
    if ((width % 2u) != 0 || (height % 2u) != 0) {
        throw std::runtime_error("NV12 sample texture dimensions must be even");
    }

    std::vector<uint8_t> y(static_cast<size_t>(width) * height);
    std::vector<uint8_t> uv(static_cast<size_t>(width) * (height / 2u), 128);

    for (UINT py = 0; py < height; ++py) {
        for (UINT px = 0; px < width; ++px) {
            y[static_cast<size_t>(py) * width + px] =
                static_cast<uint8_t>((px * 255u) / std::max(1u, width - 1u));
        }
    }

    auto tex = CreateTexture2D(core, width, height, DXGI_FORMAT_NV12, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12UploadBuffer upload;
    upload.Initialize(core.GetDevice(), GetRequiredUploadSize(core, tex, 0, 2));

    D3D12TextureSubresourceData subresources[2] = {};
    subresources[0].data = y.data();
    subresources[0].rowPitch = width;
    subresources[1].data = uv.data();
    subresources[1].rowPitch = width;

    auto ctx = core.CreateDirectContext();
    ctx.Reset();
    RecordUploadTextureSubresources(
        core,
        ctx,
        tex,
        upload,
        subresources,
        0,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ExecuteAndWait(core, ctx);

    tex.SetState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return tex;
}

void RecordCustomFusedShader(
    D3D12ProcessingContext& processing,
    D3D12CommandContext& ctx,
    D3D12Resource& srcNv12,
    D3D12Resource& dstRgba) {

    ShaderCompileDesc compileDesc = {};
    compileDesc.sourcePath = SampleShaderPath();
    compileDesc.entryPoint = "main";
    compileDesc.target = "cs_5_1";
    compileDesc.includeDirs.push_back(processing.ShaderDirectory());
    compileDesc.includeDirs.push_back(compileDesc.sourcePath.parent_path());

    const auto bytecode = CompileShaderFromFile(compileDesc);

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.numSrvs = 2;
    pipelineDesc.numUavs = 1;
    pipelineDesc.numRootConstantValues = static_cast<UINT>(sizeof(ProcessingConstantsForSample) / 4);

    D3D12ComputePipeline pipeline;
    pipeline.InitializeWithTemplate(processing.GetDevice(), bytecode, pipelineDesc);

    auto srcViews = CreateYuv420SrvViewSet(processing, srcNv12);
    auto dstViews = CreateRgbaTextureViewSet(processing, dstRgba, false, true, DXGI_FORMAT_R8G8B8A8_UNORM);

    ProcessingConstantsForSample constants = {};
    constants.srcWidth = static_cast<UINT>(srcNv12.GetWidth());
    constants.srcHeight = srcNv12.GetHeight();
    constants.dstWidth = static_cast<UINT>(dstRgba.GetWidth());
    constants.dstHeight = dstRgba.GetHeight();
    constants.srcFormat = static_cast<UINT>(DXGI_FORMAT_NV12);
    constants.dstFormat = static_cast<UINT>(DXGI_FORMAT_R8G8B8A8_UNORM);
    constants.srcMatrix = static_cast<UINT>(ProcessingColorMatrix::BT709);
    constants.srcRange = static_cast<UINT>(ProcessingColorRange::Full);
    constants.dstMatrix = static_cast<UINT>(ProcessingColorMatrix::BT709);
    constants.dstRange = static_cast<UINT>(ProcessingColorRange::Full);
    constants.filter = static_cast<UINT>(ProcessingFilter::Linear);
    constants.alphaMode = static_cast<UINT>(ProcessingAlphaMode::Ignore);
    constants.scaleX = static_cast<float>(constants.srcWidth) / static_cast<float>(constants.dstWidth);
    constants.scaleY = static_cast<float>(constants.srcHeight) / static_cast<float>(constants.dstHeight);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT barrierCount = 0;
    if (srcNv12.GetState() != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount++] = MakeTransitionBarrier(
            srcNv12.Get(),
            srcNv12.GetState(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        srcNv12.SetState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (dstRgba.GetState() != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount++] = MakeTransitionBarrier(
            dstRgba.Get(),
            dstRgba.GetState(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dstRgba.SetState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (barrierCount != 0) {
        ctx.ResourceBarrier(barrierCount, barriers);
    }

    auto* cmd = ctx.GetCommandList();
    ID3D12DescriptorHeap* heaps[] = { processing.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    pipeline.Bind(ctx);
    cmd->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srcViews.Gpu(srcViews.ySrvIndex));
    cmd->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    cmd->SetComputeRoot32BitConstants(
        pipeline.RootConstantsIndex(),
        static_cast<UINT>(sizeof(ProcessingConstantsForSample) / 4),
        &constants,
        0);

    pipeline.Dispatch(
        ctx,
        DivideRoundUp(constants.dstWidth, kThreadGroupX),
        DivideRoundUp(constants.dstHeight, kThreadGroupY),
        1);

    ctx.ResourceBarrier(MakeUavBarrier(dstRgba.Get()));
}

} // namespace

int main() {
    try {
        D3D12CoreConfig cfg = {};
        cfg.enableDebugLayer = false;
        cfg.enableInfoQueue = false;
        cfg.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(cfg);

        D3D12DescriptorAllocator cbvSrvUav;
        D3D12DescriptorAllocator sampler;
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);

        D3D12ProcessingContext processing;
        processing.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());

        if (!processing.SupportsNv12Srv() || !processing.SupportsRgba8Uav()) {
            std::cout << "Required NV12 SRV or RGBA8 UAV support is unavailable on this device.\n";
            return 0;
        }

        constexpr UINT srcWidth = 128;
        constexpr UINT srcHeight = 72;
        constexpr UINT dstWidth = 64;
        constexpr UINT dstHeight = 36;

        auto src = CreateUploadedNv12Gradient(*core, srcWidth, srcHeight);
        auto dst = CreateTexture2D(
            *core,
            dstWidth,
            dstHeight,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        auto ctx = core->CreateDirectContext();
        ctx.Reset();
        RecordCustomFusedShader(processing, ctx, src, dst);
        ExecuteAndWait(*core, ctx);

        std::cout << "RESULT: OK - custom HLSL fused NV12 -> RGB -> resize -> outside darken dispatch completed. Output: "
                  << dst.GetWidth() << "x" << dst.GetHeight() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "D3D12Sample_18_ProcessingCustomFusedShader failed: " << e.what() << "\n";
        return 1;
    }
}
