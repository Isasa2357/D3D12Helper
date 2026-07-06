#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(runtimeDir / "PyramidDownsample2xRgba.hlsl")) {
        return runtimeDir;
    }
#ifdef D3D12HELPER_SAMPLE_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_SAMPLE_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (std::filesystem::exists(sourceDir / "PyramidDownsample2xRgba.hlsl")) {
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

std::vector<uint8_t> MakeCheckerRgba(UINT width, UINT height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u, 0);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const bool bright = ((x / 8u) + (y / 8u)) % 2u == 0u;
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            pixels[i + 0] = bright ? 220 : 40;
            pixels[i + 1] = bright ? 220 : 40;
            pixels[i + 2] = bright ? 220 : 40;
            pixels[i + 3] = 255;
        }
    }
    return pixels;
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

        if (!processing.SupportsRgba8Uav()) {
            std::cout << "R8G8B8A8 UAV typed store is not supported; skipping sample.\n";
            return 0;
        }

        constexpr UINT width = 64;
        constexpr UINT height = 64;

        const auto pixels = MakeCheckerRgba(width, height);
        auto src = CreateTexture2DFromRGBA(
            *core,
            pixels.data(),
            width,
            height,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        D3D12PyramidProcessor pyramid;
        pyramid.Initialize(processing);

        auto half = pyramid.CreateDownsampledTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
        auto restored = pyramid.CreateUpsampledTexture(*core, width / 2u, height / 2u, DXGI_FORMAT_R8G8B8A8_UNORM);

        PyramidDownsampleDesc down = {};
        PyramidUpsampleDesc up = {};
        up.filter = ProcessingFilter::Linear;

        auto ctx = core->CreateDirectContext();
        ctx.Reset();
        pyramid.RecordDownsample2x(ctx, src, half, down);
        pyramid.RecordUpsample2x(ctx, half, restored, up);
        ExecuteAndWait(*core, ctx);

        std::cout << "RESULT: OK - pyramid downsample/upsample dispatch completed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "D3D12Sample_16_ProcessingPyramid failed: " << e.what() << "\n";
        return 1;
    }
}
