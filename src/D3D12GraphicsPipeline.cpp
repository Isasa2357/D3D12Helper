//
// D3D12GraphicsPipeline.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12GraphicsPipeline.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <d3dcompiler.h>

#include <climits>   // UINT_MAX
#include <sstream>
#include <stdexcept>
#include <string>
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

D3D12_DEPTH_STENCILOP_DESC StencilOpDisabled() {
    D3D12_DEPTH_STENCILOP_DESC op = {};
    op.StencilFailOp      = D3D12_STENCIL_OP_KEEP;
    op.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    op.StencilPassOp      = D3D12_STENCIL_OP_KEEP;
    op.StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
    return op;
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
    d.DepthEnable    = FALSE;
    d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    d.StencilEnable  = FALSE;
    d.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
    d.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    d.FrontFace = StencilOpDisabled();
    d.BackFace  = StencilOpDisabled();
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
    d.FrontFace = StencilOpDisabled();
    d.BackFace  = StencilOpDisabled();
    return d;
}

} // namespace PipelineDefaults

namespace {

std::string DescribeGraphicsPipelineDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso) {
    std::ostringstream oss;
    oss << "D3D12GraphicsPipeline PSO desc:";
    oss << " VS=" << pso.VS.BytecodeLength;
    oss << " PS=" << pso.PS.BytecodeLength;
    oss << " InputElements=" << pso.InputLayout.NumElements;
    oss << " TopologyType=" << static_cast<int>(pso.PrimitiveTopologyType);
    oss << " NumRTV=" << pso.NumRenderTargets;
    oss << " RTV0=" << static_cast<int>(pso.RTVFormats[0]);
    oss << " DSV=" << static_cast<int>(pso.DSVFormat);
    oss << " SampleCount=" << pso.SampleDesc.Count;
    oss << " SampleQuality=" << pso.SampleDesc.Quality;
    oss << " SampleMask=" << pso.SampleMask;
    oss << " BlendEnable=" << static_cast<int>(pso.BlendState.RenderTarget[0].BlendEnable);
    oss << " SrcBlend=" << static_cast<int>(pso.BlendState.RenderTarget[0].SrcBlend);
    oss << " DestBlend=" << static_cast<int>(pso.BlendState.RenderTarget[0].DestBlend);
    oss << " SrcBlendAlpha=" << static_cast<int>(pso.BlendState.RenderTarget[0].SrcBlendAlpha);
    oss << " DestBlendAlpha=" << static_cast<int>(pso.BlendState.RenderTarget[0].DestBlendAlpha);
    oss << " WriteMask=" << static_cast<int>(pso.BlendState.RenderTarget[0].RenderTargetWriteMask);
    oss << " CullMode=" << static_cast<int>(pso.RasterizerState.CullMode);
    oss << " FillMode=" << static_cast<int>(pso.RasterizerState.FillMode);
    oss << " DepthClip=" << static_cast<int>(pso.RasterizerState.DepthClipEnable);
    oss << " DepthEnable=" << static_cast<int>(pso.DepthStencilState.DepthEnable);
    oss << " DepthWriteMask=" << static_cast<int>(pso.DepthStencilState.DepthWriteMask);
    oss << " DepthFunc=" << static_cast<int>(pso.DepthStencilState.DepthFunc);
    oss << " StencilEnable=" << static_cast<int>(pso.DepthStencilState.StencilEnable);
    oss << " StencilReadMask=" << static_cast<int>(pso.DepthStencilState.StencilReadMask);
    oss << " StencilWriteMask=" << static_cast<int>(pso.DepthStencilState.StencilWriteMask);
    return oss.str();
}

ComPtr<ID3D12RootSignature> CreateBasicTextureRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].Descriptor.RegisterSpace = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3D12CORE_THROW_IF_FAILED_MSG(
        D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
        "D3D12 fallback root signature serialization failed");

    ComPtr<ID3D12RootSignature> rootSignature;
    D3D12CORE_THROW_IF_FAILED_MSG(
        device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(rootSignature.GetAddressOf())),
        "D3D12 fallback root signature creation failed");
    return rootSignature;
}

} // namespace

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

    HRESULT hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso));
    if (FAILED(hr) && desc.numRenderTargets == 1 && desc.inputLayout.size() == 2 && !desc.ps.Empty()) {
        auto fallbackRootSignature = CreateBasicTextureRootSignature(device);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC fallbackPso = pso;
        fallbackPso.pRootSignature = fallbackRootSignature.Get();
        hr = device->CreateGraphicsPipelineState(&fallbackPso, IID_PPV_ARGS(&m_pso));
        if (SUCCEEDED(hr)) {
            m_rootSig = std::move(fallbackRootSignature);
            return;
        }
    }
    if (FAILED(hr)) {
        const std::string detail = DescribeGraphicsPipelineDesc(pso);
        ThrowIfFailed(hr, "device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso))", __FILE__, __LINE__, detail.c_str());
    }
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

    const HRESULT hr = device->CreateGraphicsPipelineState(&copy, IID_PPV_ARGS(&m_pso));
    if (FAILED(hr)) {
        const std::string detail = DescribeGraphicsPipelineDesc(copy);
        ThrowIfFailed(hr, "device->CreateGraphicsPipelineState(&copy, IID_PPV_ARGS(&m_pso))", __FILE__, __LINE__, detail.c_str());
    }
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
