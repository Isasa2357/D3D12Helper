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

    D3D12_RASTERIZER_DESC wire = PipelineDefaults::Rasterizer(
        D3D12_CULL_MODE_FRONT,
        D3D12_FILL_MODE_WIREFRAME,
        true);
    CHECK(wire.FillMode == D3D12_FILL_MODE_WIREFRAME);
    CHECK(wire.CullMode == D3D12_CULL_MODE_FRONT);
    CHECK(wire.FrontCounterClockwise == TRUE);
    CHECK(wire.ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF);

    D3D12_BLEND_DESC opaque = PipelineDefaults::BlendOpaque();
    CHECK(opaque.RenderTarget[0].BlendEnable == FALSE);
    CHECK(opaque.RenderTarget[0].LogicOpEnable == FALSE);
    CHECK_EQ((int)opaque.RenderTarget[0].RenderTargetWriteMask,
             (int)D3D12_COLOR_WRITE_ENABLE_ALL);

    D3D12_BLEND_DESC alpha = PipelineDefaults::BlendAlpha();
    CHECK(alpha.RenderTarget[0].BlendEnable == TRUE);
    CHECK(alpha.RenderTarget[0].SrcBlend == D3D12_BLEND_SRC_ALPHA);
    CHECK(alpha.RenderTarget[0].DestBlend == D3D12_BLEND_INV_SRC_ALPHA);
    CHECK(alpha.RenderTarget[0].SrcBlendAlpha == D3D12_BLEND_ONE);

    CHECK(PipelineDefaults::DepthDisabled().DepthEnable == FALSE);
    CHECK(PipelineDefaults::DepthDisabled().DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ZERO);

    auto depthDefault = PipelineDefaults::DepthDefault();
    CHECK(depthDefault.DepthEnable == TRUE);
    CHECK(depthDefault.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL);
    CHECK(depthDefault.DepthFunc == D3D12_COMPARISON_FUNC_LESS);

    auto depthReadOnly = PipelineDefaults::DepthDefault(false, D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    CHECK(depthReadOnly.DepthEnable == TRUE);
    CHECK(depthReadOnly.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ZERO);
    CHECK(depthReadOnly.DepthFunc == D3D12_COMPARISON_FUNC_GREATER_EQUAL);
}

TEST(GraphicsPipeline, BindRejectsUninitializedPipeline) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    D3D12GraphicsPipeline pipeline;
    CHECK_THROWS(pipeline.Bind(ctx));
    CHECK_THROWS(pipeline.Bind(static_cast<ID3D12GraphicsCommandList*>(nullptr)));
    ctx.Close();
}

namespace {
const char* kTriHlsl = R"(
float4 VSMain(float3 pos : POSITION) : SV_POSITION { return float4(pos, 1.0f); }
float4 PSMain() : SV_TARGET { return float4(1.0f, 0.0f, 0.0f, 1.0f); }
)";

const char* kVertexIdHlsl = R"(
float4 VSMain(uint vid : SV_VertexID) : SV_POSITION
{
    float2 p[3] = { float2(-1.0f, -1.0f), float2(3.0f, -1.0f), float2(-1.0f, 3.0f) };
    return float4(p[vid], 0.0f, 1.0f);
}
float4 PSMain() : SV_TARGET { return float4(0.0f, 1.0f, 0.0f, 1.0f); }
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

GraphicsPipelineDesc MakeBasicGraphicsDesc(ShaderBytecode vs, ShaderBytecode ps, DXGI_FORMAT fmt) {
    GraphicsPipelineDesc gd;
    gd.vs = std::move(vs);
    gd.ps = std::move(ps);
    gd.inputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    gd.rtvFormats[0] = fmt;
    return gd;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeRawPsoDesc(
    ID3D12RootSignature* rootSig,
    const ShaderBytecode& vs,
    const ShaderBytecode& ps,
    const D3D12_INPUT_ELEMENT_DESC* inputLayout,
    UINT inputElementCount,
    DXGI_FORMAT fmt) {

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = rootSig;
    pso.VS = vs.AsD3D12();
    pso.PS = ps.AsD3D12();
    pso.BlendState = PipelineDefaults::BlendOpaque();
    pso.RasterizerState = PipelineDefaults::Rasterizer();
    pso.DepthStencilState = PipelineDefaults::DepthDisabled();
    pso.SampleMask = UINT_MAX;
    pso.InputLayout = { inputLayout, inputElementCount };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = fmt;
    pso.SampleDesc.Count = 1;
    return pso;
}

} // namespace

TEST(GraphicsPipeline, InitializeRejectsInvalidArguments) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();
    const auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriHlsl, "PSMain", "ps_6_0");

    D3D12GraphicsPipeline pipeline;
    GraphicsPipelineDesc desc = MakeBasicGraphicsDesc(vs, ps, fmt);
    CHECK_THROWS(pipeline.Initialize(nullptr, MakeEmptyRootSig(device), desc));
    CHECK_THROWS(pipeline.Initialize(device, nullptr, desc));

    GraphicsPipelineDesc emptyVs = desc;
    emptyVs.vs = ShaderBytecode{};
    CHECK_THROWS(pipeline.Initialize(device, MakeEmptyRootSig(device), emptyVs));

    GraphicsPipelineDesc tooManyRts = desc;
    tooManyRts.numRenderTargets = 9;
    CHECK_THROWS(pipeline.Initialize(device, MakeEmptyRootSig(device), tooManyRts));
}

TEST(GraphicsPipeline, InitializeRawRejectsInvalidArguments) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();
    const auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriHlsl, "PSMain", "ps_6_0");
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    auto rootSig = MakeEmptyRootSig(device);
    auto raw = MakeRawPsoDesc(rootSig.Get(), vs, ps, layout, 1, fmt);

    D3D12GraphicsPipeline pipeline;
    CHECK_THROWS(pipeline.InitializeRaw(nullptr, rootSig, raw));
    CHECK_THROWS(pipeline.InitializeRaw(device, nullptr, raw));
}

TEST(GraphicsPipeline, InitializeRawCreatesPipeline) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();
    const auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriHlsl, "PSMain", "ps_6_0");
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    auto rootSig = MakeEmptyRootSig(device);
    auto raw = MakeRawPsoDesc(rootSig.Get(), vs, ps, layout, 1, fmt);

    D3D12GraphicsPipeline pipeline;
    pipeline.InitializeRaw(device, rootSig, raw);
    CHECK(pipeline.GetRootSignature() != nullptr);
    CHECK(pipeline.GetPipelineState() != nullptr);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_NOTHROW(pipeline.Bind(ctx));
    ctx.Close();
}

TEST(GraphicsPipeline, InitializeWithCustomStatesAndDepthFormat) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();
    ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriHlsl, "PSMain", "ps_6_0");

    GraphicsPipelineDesc desc = MakeBasicGraphicsDesc(std::move(vs), std::move(ps), DXGI_FORMAT_R8G8B8A8_UNORM);
    desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    desc.sampleCount = 1;
    desc.sampleQuality = 0;

    auto rasterizer = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID, true);
    auto blend = PipelineDefaults::BlendAlpha();
    auto depth = PipelineDefaults::DepthDefault(false, D3D12_COMPARISON_FUNC_LESS_EQUAL);
    desc.rasterizer = &rasterizer;
    desc.blend = &blend;
    desc.depthStencil = &depth;

    D3D12GraphicsPipeline pipeline;
    pipeline.Initialize(device, MakeEmptyRootSig(device), desc);
    CHECK(pipeline.GetRootSignature() != nullptr);
    CHECK(pipeline.GetPipelineState() != nullptr);
}

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

TEST(GraphicsPipeline, OffscreenDrawsVertexIdTriangleWithoutInputLayout) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();

    const UINT size = 32;
    const auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12Resource rt = CreateTexture2D(
        *core, size, size, fmt,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12DescriptorAllocator rtvAlloc;
    rtvAlloc.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    D3D12DescriptorHandle rtv = rtvAlloc.Allocate();
    CreateTexture2DRtv(*core, rt, rtv.cpu);

    ShaderBytecode vs = CompileShaderFromSource_Dxc(kVertexIdHlsl, "VSMain", "vs_6_0");
    ShaderBytecode ps = CompileShaderFromSource_Dxc(kVertexIdHlsl, "PSMain", "ps_6_0");
    GraphicsPipelineDesc gd;
    gd.vs = std::move(vs);
    gd.ps = std::move(ps);
    gd.rtvFormats[0] = fmt;

    D3D12GraphicsPipeline pipeline;
    pipeline.Initialize(device, MakeEmptyRootSig(device), gd);

    D3D12_RESOURCE_DESC rd = rt.GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSize, &total);
    D3D12ReadbackBuffer readback;
    readback.Initialize(device, total);

    auto ctx = core->CreateDirectContext();
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
    cl->DrawInstanced(3, 1, 0, 0);
    ctx.ResourceBarrier(MakeTransitionBarrier(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

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

    CHECK(R < 60);
    CHECK(G > 200);
    CHECK(B < 60);
}
