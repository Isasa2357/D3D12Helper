#include <D3D12Helper/D3D12Presentation/D3D12SwapChain.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12RenderTarget.hpp>
#include <D3D12Helper/D3D12Framework/D3D12SwapChainHelper.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace D3D12CoreLib {

void D3D12SwapChain::Initialize(D3D12Core& core, const D3D12SwapChainDesc& desc) {
    if (!desc.hwnd) throw std::runtime_error("D3D12SwapChain: null HWND");
    if (desc.width == 0 || desc.height == 0) throw std::runtime_error("D3D12SwapChain: width and height must be > 0");
    if (desc.bufferCount < 2) throw std::runtime_error("D3D12SwapChain: bufferCount must be >= 2");
    if (desc.format == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("D3D12SwapChain: format must not be UNKNOWN");

    Reset();
    m_core = &core;
    m_desc = desc;
    m_swapChain = CreateSwapChainForHwnd(core, desc.hwnd, desc.width, desc.height, desc.bufferCount, desc.format);
    CreateBackBufferViews();
}

void D3D12SwapChain::Reset() noexcept {
    ReleaseBackBuffers();
    m_swapChain.Reset();
    m_rtvAllocator = {};
    m_rtvRange = {};
    m_core = nullptr;
    m_desc = {};
}

void D3D12SwapChain::ReleaseBackBuffers() noexcept {
    m_backBuffers.clear();
}

void D3D12SwapChain::CreateBackBufferViews() {
    if (!m_core || !m_swapChain) throw std::runtime_error("D3D12SwapChain: not initialized");
    m_rtvAllocator.Initialize(m_core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, m_desc.bufferCount, false);
    m_rtvRange = m_rtvAllocator.AllocateRange(m_desc.bufferCount);
    m_backBuffers.clear();
    m_backBuffers.reserve(m_desc.bufferCount);
    for (UINT i = 0; i < m_desc.bufferCount; ++i) {
        auto backBuffer = GetSwapChainBackBuffer(m_swapChain.Get(), i);
        m_core->GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtvRange.Cpu(i));
        m_backBuffers.push_back(std::move(backBuffer));
    }
}

void D3D12SwapChain::Resize(UINT width, UINT height) {
    if (!IsValid()) throw std::runtime_error("D3D12SwapChain::Resize: not initialized");
    if (width == 0 || height == 0) throw std::runtime_error("D3D12SwapChain::Resize: width and height must be > 0");
    m_core->DirectQueue().WaitIdle();
    ReleaseBackBuffers();
    D3D12CORE_THROW_IF_FAILED(m_swapChain->ResizeBuffers(m_desc.bufferCount, width, height, m_desc.format, 0));
    m_desc.width = width;
    m_desc.height = height;
    CreateBackBufferViews();
}

void D3D12SwapChain::ResizeToClientRect(HWND hwnd) {
    HWND target = hwnd ? hwnd : m_desc.hwnd;
    if (!target) throw std::runtime_error("D3D12SwapChain::ResizeToClientRect: null HWND");
    RECT rc{};
    if (!GetClientRect(target, &rc)) throw std::runtime_error("D3D12SwapChain::ResizeToClientRect: GetClientRect failed");
    const UINT w = static_cast<UINT>(rc.right - rc.left);
    const UINT h = static_cast<UINT>(rc.bottom - rc.top);
    if (w == 0 || h == 0) return;
    if (w != m_desc.width || h != m_desc.height) Resize(w, h);
}

void D3D12SwapChain::Present(UINT syncInterval, UINT flags) {
    if (!IsValid()) throw std::runtime_error("D3D12SwapChain::Present: not initialized");
    const UINT si = (syncInterval == UINT(-1)) ? m_desc.syncInterval : syncInterval;
    const UINT pf = (flags == UINT(-1)) ? m_desc.presentFlags : flags;
    D3D12CORE_THROW_IF_FAILED(m_swapChain->Present(si, pf));
}

UINT D3D12SwapChain::CurrentBackBufferIndex() const {
    if (!IsValid()) throw std::runtime_error("D3D12SwapChain::CurrentBackBufferIndex: not initialized");
    return m_swapChain->GetCurrentBackBufferIndex();
}

D3D12Resource& D3D12SwapChain::CurrentBackBuffer() {
    return m_backBuffers.at(CurrentBackBufferIndex());
}

const D3D12Resource& D3D12SwapChain::CurrentBackBuffer() const {
    return m_backBuffers.at(CurrentBackBufferIndex());
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12SwapChain::CurrentRtv() const {
    return m_rtvRange.Cpu(CurrentBackBufferIndex());
}

D3D12_VIEWPORT D3D12SwapChain::Viewport() const noexcept { return MakeViewport(m_desc.width, m_desc.height); }
D3D12_RECT D3D12SwapChain::ScissorRect() const noexcept { return MakeScissorRect(m_desc.width, m_desc.height); }

void D3D12SwapChain::BindCurrent(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12SwapChain::BindCurrent: null command list");
    auto rtv = CurrentRtv();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}

void D3D12SwapChain::ClearCurrent(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12SwapChain::ClearCurrent: null command list");
    commandList->ClearRenderTargetView(CurrentRtv(), m_desc.clearColor, 0, nullptr);
}

void D3D12SwapChain::SetViewportAndScissor(ID3D12GraphicsCommandList* commandList) const {
    if (!commandList) throw std::runtime_error("D3D12SwapChain::SetViewportAndScissor: null command list");
    const auto vp = Viewport();
    const auto sc = ScissorRect();
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &sc);
}

void D3D12SwapChain::TransitionCurrent(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES after) {
    if (!commandList) throw std::runtime_error("D3D12SwapChain::TransitionCurrent: null command list");
    auto& bb = CurrentBackBuffer();
    const auto before = bb.GetState();
    if (before == after) return;
    auto barrier = MakeTransitionBarrier(bb.Get(), before, after);
    commandList->ResourceBarrier(1, &barrier);
    bb.SetState(after);
}

D3D12SwapChain CreateSwapChain(D3D12Core& core, const D3D12SwapChainDesc& desc) {
    D3D12SwapChain sc;
    sc.Initialize(core, desc);
    return sc;
}

} // namespace D3D12CoreLib
