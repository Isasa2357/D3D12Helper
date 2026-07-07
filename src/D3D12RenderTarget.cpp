#include <D3D12Helper/D3D12Presentation/D3D12RenderTarget.hpp>
#include <D3D12Helper/D3D12Foundation/D3D12FormatUtil.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace D3D12CoreLib {
namespace {

D3D12_HEAP_PROPERTIES DefaultHeapProps() {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask = 1;
    hp.VisibleNodeMask = 1;
    return hp;
}

D3D12Resource CreateColorTarget(D3D12Core& core, const D3D12RenderTargetDesc& desc) {
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width;
    rd.Height = desc.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = desc.colorFormat;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = desc.colorFormat;
    clear.Color[0] = desc.clearColor[0];
    clear.Color[1] = desc.clearColor[1];
    clear.Color[2] = desc.clearColor[2];
    clear.Color[3] = desc.clearColor[3];

    ComPtr<ID3D12Resource> res;
    auto hp = DefaultHeapProps();
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&res)));
    return D3D12Resource(std::move(res), D3D12_RESOURCE_STATE_RENDER_TARGET);
}

D3D12Resource CreateDepthTarget(D3D12Core& core, const D3D12RenderTargetDesc& desc) {
    if (desc.depthFormat == DXGI_FORMAT_UNKNOWN) return {};
    if (!FormatUtil::IsDepthFormat(desc.depthFormat)) {
        throw std::runtime_error("D3D12RenderTarget: depthFormat is not a depth format");
    }

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width;
    rd.Height = desc.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = desc.depthFormat;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = desc.depthFormat;
    clear.DepthStencil.Depth = desc.clearDepth;
    clear.DepthStencil.Stencil = desc.clearStencil;

    ComPtr<ID3D12Resource> res;
    auto hp = DefaultHeapProps();
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&res)));
    return D3D12Resource(std::move(res), D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

} // namespace

D3D12_VIEWPORT MakeViewport(UINT width, UINT height, FLOAT minDepth, FLOAT maxDepth) {
    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<FLOAT>(width);
    vp.Height = static_cast<FLOAT>(height);
    vp.MinDepth = minDepth;
    vp.MaxDepth = maxDepth;
    return vp;
}

D3D12_RECT MakeScissorRect(UINT width, UINT height) {
    D3D12_RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = static_cast<LONG>(width);
    rect.bottom = static_cast<LONG>(height);
    return rect;
}

void D3D12RenderTarget::Initialize(D3D12Core& core, const D3D12RenderTargetDesc& desc) {
    if (desc.width == 0 || desc.height == 0) throw std::runtime_error("D3D12RenderTarget: width and height must be > 0");
    if (desc.colorFormat == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("D3D12RenderTarget: colorFormat must not be UNKNOWN");
    if (FormatUtil::IsDepthFormat(desc.colorFormat)) throw std::runtime_error("D3D12RenderTarget: colorFormat must not be a depth format");

    Reset();
    m_desc = desc;
    m_color = CreateColorTarget(core, desc);
    m_rtvAllocator.Initialize(core.GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    m_rtv = m_rtvAllocator.Allocate();
    CreateTexture2DRtv(core, m_color, m_rtv.cpu, desc.colorFormat);

    if (desc.depthFormat != DXGI_FORMAT_UNKNOWN) {
        m_depth = CreateDepthTarget(core, desc);
        m_dsvAllocator.Initialize(core.GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
        m_dsv = m_dsvAllocator.Allocate();
        CreateTexture2DDsv(core, m_depth, m_dsv.cpu, desc.depthFormat);
    }
}

void D3D12RenderTarget::Reset() noexcept {
    m_desc = {};
    m_color = {};
    m_depth = {};
    m_rtvAllocator = {};
    m_dsvAllocator = {};
    m_rtv = {};
    m_dsv = {};
}

D3D12_VIEWPORT D3D12RenderTarget::Viewport() const noexcept { return MakeViewport(m_desc.width, m_desc.height); }
D3D12_RECT D3D12RenderTarget::ScissorRect() const noexcept { return MakeScissorRect(m_desc.width, m_desc.height); }

void D3D12RenderTarget::Bind(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12RenderTarget::Bind: null command list");
    if (!IsValid()) throw std::runtime_error("D3D12RenderTarget::Bind: not initialized");
    if (m_dsv.IsValid()) commandList->OMSetRenderTargets(1, &m_rtv.cpu, FALSE, &m_dsv.cpu);
    else commandList->OMSetRenderTargets(1, &m_rtv.cpu, FALSE, nullptr);
}

void D3D12RenderTarget::Clear(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12RenderTarget::Clear: null command list");
    if (!IsValid()) throw std::runtime_error("D3D12RenderTarget::Clear: not initialized");
    commandList->ClearRenderTargetView(m_rtv.cpu, m_desc.clearColor, 0, nullptr);
    if (m_dsv.IsValid()) {
        commandList->ClearDepthStencilView(m_dsv.cpu, D3D12_CLEAR_FLAG_DEPTH, m_desc.clearDepth, m_desc.clearStencil, 0, nullptr);
    }
}

void D3D12RenderTarget::SetViewportAndScissor(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12RenderTarget::SetViewportAndScissor: null command list");
    const auto vp = Viewport();
    const auto scissor = ScissorRect();
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &scissor);
}

D3D12RenderTarget CreateRenderTarget(D3D12Core& core, const D3D12RenderTargetDesc& desc) {
    D3D12RenderTarget rt;
    rt.Initialize(core, desc);
    return rt;
}

} // namespace D3D12CoreLib
