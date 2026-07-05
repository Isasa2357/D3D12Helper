//
// test_GraphicsPipeline.cpp - PipelineDefaults と D3D12GraphicsPipeline のオフスクリーン描画
//
#include "TestCommon.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

using namespace D3D12CoreLib;

// --- PipelineDefaults は device 不要で検証できる ---
TEST(GraphicsPipeline, PipelineDefaults) {
    D3D12_RASTERIZER_DESC r = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
    CHECK(r.FillMode == D3D12_FILL_MODE_SOLID);
    CHECK(r.CullMode == D3D12_CULL_MODE_NONE);
    CHECK(r.DepthClipEnable == TRUE);

    D3D12_BLEND_DESC opaque = PipelineDefaults::BlendOpaque();
    CHECK(opaque.RenderTarget[0].BlendEnable == FALSE);
    CHECK_EQ((int)opaque.RenderTarget[0].RenderTargetWriteMask,
             (int)D3D12_COLOR_WRITE_ENABLE_ALL);

    D3D12_BLEND_DESC alpha = PipelineDefaults::BlendAlpha();
    CHECK(alpha.RenderTarget[0].BlendEnable == TRUE);
    CHECK(alpha.RenderTarget[0].SrcBlend == D3D12_BLEND_SRC_ALPHA);

    CHECK(PipelineDefaults::DepthDisabled().DepthEnable == FALSE);
    CHECK(PipelineDefaults::DepthDefault().DepthEnable == TRUE);
}

TEST(GraphicsPipeline, BindRejectsUninitializedPipeline) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    D3D12GraphicsPipeline pipeline;
    CHECK_THROWS(pipeline.Bind(ctx));
    ctx.Close();
}

namespace {
const char* kTriHlsl = R"(
float4 VSMain(float3 pos : POSITION) : SV_POSITION { return float4(pos, 1.0f); }
float4 PSMain() : SV_TARGET { return float4(1.0f, 0.0f, 0.0f, 1.0f); }
)";

ComPtr<ID3D12RootSignature> MakeEmptyRootSig(ID3D12Device* device) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC v = {};
    v.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    v.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeVersionedRootSignature(&v, &blob, &err)))
        throw std::runtime_error("serialize root sig failed");
    ComPtr<ID3D12RootSignature> rs;
    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs)));
    return rs;
}
} // namespace

// --- フルスクリーン三角形をオフスクリーン RT に描いて中央ピクセルを検証 ---
TEST(GraphicsPipeline, OffscreenDrawsTriangle) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();

    const UINT size = 64;
    const auto fmt  = DXGI_FORMAT_R8G8B8A8_UNORM;

    // RT テクスチャ
    D3D12Resource rt = CreateTexture2D(
        *core, size, size, fmt,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12DescriptorAllocator rtvAlloc;
    rtvAlloc.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    D3D12DescriptorHandle rtv = rtvAlloc.Allocate();
    CreateTexture2DRtv(*core, rt, rtv.cpu);

    // パイプライン
    ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriHlsl, "PSMain", "ps_6_0");
    ComPtr<ID3D12RootSignature> rootSig = MakeEmptyRootSig(device);

    GraphicsPipelineDesc gd;
    gd.vs = std::move(vs);
    gd.ps = std::move(ps);
    gd.inputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    gd.rtvFormats[0] = fmt;
    D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
    gd.rasterizer = &noCull;

    D3D12GraphicsPipeline pipeline;
    pipeline.Initialize(device, rootSig, gd);
    CHECK(pipeline.GetPipelineState() != nullptr);

    // 画面全体を覆う特大三角形
    struct V { float p[3]; };
    const V verts[] = { {{-1,-1,0}}, {{ 3,-1,0}}, {{-1, 3,0}} };
    D3D12UploadBuffer vb;
    vb.Initialize(device, sizeof(verts));
    std::memcpy(vb.Map(), verts, sizeof(verts));
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = vb.Get()->GetGPUVirtualAddress();
    vbv.SizeInBytes = sizeof(verts);
    vbv.StrideInBytes = sizeof(V);

    // リードバック
    D3D12_RESOURCE_DESC rd = rt.GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSize, &total);
    D3D12ReadbackBuffer readback;
    readback.Initialize(device, total);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    auto* cl = ctx.GetCommandList();

    const D3D12_VIEWPORT vp = { 0, 0, (float)size, (float)size, 0, 1 };
    const D3D12_RECT sc = { 0, 0, (LONG)size, (LONG)size };
    const float clear[4] = { 0, 0, 0, 1 };

    cl->OMSetRenderTargets(1, &rtv.cpu, FALSE, nullptr);
    cl->ClearRenderTargetView(rtv.cpu, clear, 0, nullptr);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);
    pipeline.Bind(ctx);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->IASetVertexBuffers(0, 1, &vbv);
    cl->DrawInstanced(3, 1, 0, 0);

    ctx.ResourceBarrier(MakeTransitionBarrier(
        rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = rt.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

    const auto* base = static_cast<const uint8_t*>(readback.Map());
    const uint8_t* center = base + (size / 2) * fp.Footprint.RowPitch + (size / 2) * 4;
    const uint8_t R = center[0], G = center[1], B = center[2];
    readback.Unmap();

    // 中央は赤で塗られているはず。
    CHECK(R > 200);
    CHECK(G < 60);
    CHECK(B < 60);
}
