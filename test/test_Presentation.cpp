#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cmath>
#include <cstdint>

using namespace D3D12CoreLib;

namespace {
void NearByte(uint8_t a, uint8_t b, int t) {
    if (std::abs(static_cast<int>(a) - static_cast<int>(b)) > t) TEST_FAIL("byte mismatch");
}
}

TEST(Presentation, RenderTargetClearReadback) {
    REQUIRE_CORE(core);

    D3D12RenderTargetDesc desc;
    desc.width = 4;
    desc.height = 3;
    desc.colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.clearColor[0] = 0.25f;
    desc.clearColor[1] = 0.50f;
    desc.clearColor[2] = 0.75f;
    desc.clearColor[3] = 1.00f;

    auto rt = CreateRenderTarget(*core, desc);
    CHECK(rt.IsValid());
    CHECK_EQ(rt.Width(), 4u);
    CHECK_EQ(rt.Height(), 3u);
    CHECK(rt.Rtv().ptr != 0);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    rt.SetViewportAndScissor(ctx.GetCommandList());
    rt.Bind(ctx.GetCommandList());
    rt.Clear(ctx.GetCommandList());
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();

    auto image = ReadbackTexture2DToCpuImage(*core, rt.ColorResource());
    const auto& p = image.planes[0];
    const uint8_t* px = image.pixels.data() + p.offsetBytes;
    NearByte(px[0], 64, 2);
    NearByte(px[1], 128, 2);
    NearByte(px[2], 191, 2);
    NearByte(px[3], 255, 1);
}

TEST(Presentation, RenderTargetValidation) {
    REQUIRE_CORE(core);
    D3D12RenderTargetDesc desc;
    desc.width = 0;
    desc.height = 4;
    CHECK_THROWS(CreateRenderTarget(*core, desc));
    desc.width = 4;
    desc.colorFormat = DXGI_FORMAT_UNKNOWN;
    CHECK_THROWS(CreateRenderTarget(*core, desc));
    desc.colorFormat = DXGI_FORMAT_D32_FLOAT;
    CHECK_THROWS(CreateRenderTarget(*core, desc));
}

TEST(Presentation, RenderTargetDepthCreation) {
    REQUIRE_CORE(core);
    D3D12RenderTargetDesc desc;
    desc.width = 2;
    desc.height = 2;
    desc.colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = DXGI_FORMAT_D32_FLOAT;
    auto rt = CreateRenderTarget(*core, desc);
    CHECK(rt.IsValid());
    CHECK(rt.DepthResource().Get() != nullptr);
    CHECK(rt.Dsv().ptr != 0);
}
