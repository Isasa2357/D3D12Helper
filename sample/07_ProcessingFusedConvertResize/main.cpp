#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(runtimeDir / "FusedYuv420ToRgbResize.hlsl")) {
        return runtimeDir;
    }
#ifdef D3D12HELPER_SAMPLE_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_SAMPLE_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "FusedYuv420ToRgbResize.hlsl")) {
        return sourceDir;
    }
#endif
    return runtimeDir;
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& ctx) {
    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

D3D12Resource CreateUploadedNv12(D3D12Core& core, UINT width, UINT height) {
    std::vector<uint8_t> y(static_cast<size_t>(width) * height);
    std::vector<uint8_t> uv(static_cast<size_t>(width) * (height / 2));
    for (UINT py = 0; py < height; ++py) {
        for (UINT px = 0; px < width; ++px) {
            y[static_cast<size_t>(py) * width + px] = static_cast<uint8_t>((px + py) * 32u);
        }
    }
    for (auto& v : uv) v = 128;

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
    RecordUploadTextureSubresources(core, ctx, tex, upload, subresources, 0, 2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ExecuteAndWait(core, ctx);
    tex.SetState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return tex;
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
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);

        D3D12ProcessingContext processing;
        processing.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());

        if (!processing.SupportsNv12Srv() || !processing.SupportsRgba8Uav()) {
            std::cout << "Required NV12 SRV or RGBA8 UAV support is unavailable on this device.\n";
            return 0;
        }

        auto src = CreateUploadedNv12(*core, 4, 4);

        D3D12FusedProcessor fused;
        fused.Initialize(processing);
        auto dst = fused.CreateOutputTexture(*core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM);

        FusedConvertResizeDesc desc = {};
        desc.srcFormat = DXGI_FORMAT_NV12;
        desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.filter = ProcessingFilter::Linear;
        desc.color.srcRange = ProcessingColorRange::Full;
        desc.color.srcMatrix = ProcessingColorMatrix::BT709;

        auto ctx = core->CreateDirectContext();
        ctx.Reset();
        fused.RecordConvertResize(ctx, src, dst, desc);
        ExecuteAndWait(*core, ctx);

        std::cout << "Fused NV12 -> RGBA resize completed. Output: "
                  << dst.GetWidth() << "x" << dst.GetHeight() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "D3D12Sample_07_ProcessingFusedConvertResize failed: " << e.what() << "\n";
        return 1;
    }
}
