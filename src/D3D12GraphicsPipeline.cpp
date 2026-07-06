//
// D3D12GraphicsPipeline.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12GraphicsPipeline.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <climits>   // UINT_MAX
#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {

namespace PipelineDefaults {

D3D12_RASTERIZER_DESC Rasterizer(D3D12_CULL_MODE cull, D3D12_FILL_MODE fill,
                                 bool frontCounterClockwise) {
    D3D12_RASTERIZER_DESC r = {};
    r.FillMode              = fill;
    r.CullMode              = cull;
    r.FrontCounterClockwise = frontCounterClockwise ? TRUE : FALSE;
    r.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    r.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    r.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    r.DepthClipEnable       = TRUE;
    r.MultisampleEnable     = FALSE;
    r.AntialiasedLineEnable = FALSE;
    r.ForcedSampleCount     = 0;
    r.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return r;
}

namespace {
D3D12_RENDER_TARGET_BLEND_DESC RtNoBlend() {
    D3D12_RENDER_TARGET_BLEND_DESC b = {};
    b.BlendEnable           = FALSE;
    b.LogicOpEnable         = FALSE;
    b.SrcBlend              = D3D12_BLEND_ONE;
    b.DestBlend             = D3D12_BLEND_ZERO;
    b.BlendOp               = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha         = D3D12_BLEND_ONE;
    b.DestBlendAlpha        = D3D12_BLEND_ZERO;
    b.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    b.LogicOp               = D3D12_LOGIC_OP_NOOP;
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return b;
}
} // unnamed namespace

D3D12_BLEND_DESC BlendOpaque() {
    D3D12_BLEND_DESC d = {};
    d.AlphaToCoverageEnable  = FALSE;
    d.IndependentBlendEnable = FALSE;
    d.RenderTarget[0]        = RtNoBlend();
    return d;
}

D3D12_BLEND_DESC BlendAlpha() {
    D3D12_BLEND_DESC d = BlendOpaque();
    D3D12_RENDER_TARGET_BLEND_DESC& b = d.RenderTarget[0];
    b.BlendEnable    = TRUE;
    b.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
    b.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp        = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha  = D3D12_BLEND_ONE;
    b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    return d;
}

D3D12_DEPTH_STENCIL_DESC DepthDisabled() {
    D3D12_DEPTH_STENCIL_DESC d = {};
    d.DepthEnable   = FALSE;
    d.StencilEnable = FALSE;
    return d;
}

D3D12_DEPTH_STENCIL_DESC DepthDefault(bool depthWrite, D3D12_COMPARISON_FUNC depthFunc) {
    D3D12_DEPTH_STENCIL_DESC d = {};
    d.DepthEnable    = TRUE;
    d.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                  : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc      = depthFunc;
    d.StencilEnable  = FALSE;
    d.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
    d.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC op = {
        D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    d.FrontFace = op;
    d.BackFace  = op;
    return d;
}

} // namespace PipelineDefaults

void D3D12GraphicsPipeline::Initialize(
    ID3D12Device* device,
    ComPtr<ID3D12RootSignature> rootSignature,
    const GraphicsPipelineDesc& desc) {

    if (!device)        throw std::runtime_error("D3D12GraphicsPipeline: null device");
    if (!rootSignature) throw std::runtime_error("D3D12GraphicsPipeline: null root signature");
    if (desc.vs.Empty()) throw std::runtime_error("D3D12GraphicsPipeline: empty vertex shader");
    if (desc.numRenderTargets > 8)
        throw std::runtime_error("D3D12GraphicsPipeline: numRenderTargets must be <= 8");

    m_rootSig = std::move(rootSignature);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_rootSig.Get();
    pso.VS = desc.vs.AsD3D12();
    pso.PS = desc.ps.AsD3D12();   // 空なら {nullptr, 0}（PS 無し）

    pso.BlendState      = desc.blend
                          ? *desc.blend
                          : PipelineDefaults::BlendOpaque();
    pso.RasterizerState = desc.rasterizer
                          ? *desc.rasterizer
                          : PipelineDefaults::Rasterizer();
    pso.DepthStencilState = desc.depthStencil
                          ? *desc.depthStencil
                          : (desc.dsvFormat == DXGI_FORMAT_UNKNOWN
                             ? PipelineDefaults::DepthDisabled()
                             : PipelineDefaults::DepthDefault());

    pso.SampleMask = UINT_MAX;

    pso.InputLayout = {
        desc.inputLayout.empty() ? nullptr : desc.inputLayout.data(),
        static_cast<UINT>(desc.inputLayout.size()) };

    pso.PrimitiveTopologyType = desc.topologyType;

    pso.NumRenderTargets = desc.numRenderTargets;
    for (UINT i = 0; i < desc.numRenderTargets; ++i) {
        pso.RTVFormats[i] = desc.rtvFormats[i];
    }
    pso.DSVFormat = desc.dsvFormat;

    pso.SampleDesc.Count   = desc.sampleCount == 0 ? 1 : desc.sampleCount;
    pso.SampleDesc.Quality = desc.sampleQuality;

    D3D12CORE_THROW_IF_FAILED(
        device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)));
}

void D3D12GraphicsPipeline::InitializeRaw(
    ID3D12Device* device,
    ComPtr<ID3D12RootSignature> rootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc) {

    if (!device)        throw std::runtime_error("D3D12GraphicsPipeline: null device");
    if (!rootSignature) throw std::runtime_error("D3D12GraphicsPipeline: null root signature");

    m_rootSig = std::move(rootSignature);

    // 保持する Root Signature と PSO の pRootSignature を一致させる。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC copy = psoDesc;
    copy.pRootSignature = m_rootSig.Get();

    D3D12CORE_THROW_IF_FAILED(
        device->CreateGraphicsPipelineState(&copy, IID_PPV_ARGS(&m_pso)));
}

void D3D12GraphicsPipeline::Bind(D3D12CommandContext& ctx) const {
    Bind(ctx.GetCommandList());
}

void D3D12GraphicsPipeline::Bind(ID3D12GraphicsCommandList* cmd) const {
    if (!cmd) throw std::runtime_error("D3D12GraphicsPipeline::Bind: null command list");
    if (!m_rootSig) throw std::runtime_error("D3D12GraphicsPipeline::Bind: pipeline is not initialized");
    if (!m_pso) throw std::runtime_error("D3D12GraphicsPipeline::Bind: pipeline state is not initialized");
    cmd->SetGraphicsRootSignature(m_rootSig.Get());
    cmd->SetPipelineState(m_pso.Get());
}

} // namespace D3D12CoreLib
