#pragma once
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorAllocator.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>
#include <vector>

namespace D3D12CoreLib {

struct D3D12SwapChainDesc {
    HWND hwnd = nullptr;
    UINT width = 0;
    UINT height = 0;
    UINT bufferCount = 2;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT syncInterval = 1;
    UINT presentFlags = 0;
    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

class D3D12SwapChain {
public:
    void Initialize(D3D12Core& core, const D3D12SwapChainDesc& desc);
    void Reset() noexcept;
    void Resize(UINT width, UINT height);
    void ResizeToClientRect(HWND hwnd = nullptr);
    void Present(UINT syncInterval = UINT(-1), UINT flags = UINT(-1));

    bool IsValid() const noexcept { return m_swapChain.Get() != nullptr; }
    UINT Width() const noexcept { return m_desc.width; }
    UINT Height() const noexcept { return m_desc.height; }
    UINT BufferCount() const noexcept { return m_desc.bufferCount; }
    DXGI_FORMAT Format() const noexcept { return m_desc.format; }
    HWND Hwnd() const noexcept { return m_desc.hwnd; }
    IDXGISwapChain3* Get() const noexcept { return m_swapChain.Get(); }

    UINT CurrentBackBufferIndex() const;
    D3D12Resource& CurrentBackBuffer();
    const D3D12Resource& CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;
    D3D12_VIEWPORT Viewport() const noexcept;
    D3D12_RECT ScissorRect() const noexcept;

    void BindCurrent(ID3D12GraphicsCommandList* commandList) const;
    void ClearCurrent(ID3D12GraphicsCommandList* commandList) const;
    void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList) const;
    void TransitionCurrent(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES after);

private:
    void CreateBackBufferViews();
    void ReleaseBackBuffers() noexcept;

    D3D12Core* m_core = nullptr;
    D3D12SwapChainDesc m_desc;
    ComPtr<IDXGISwapChain3> m_swapChain;
    D3D12DescriptorAllocator m_rtvAllocator;
    D3D12DescriptorRange m_rtvRange;
    std::vector<D3D12Resource> m_backBuffers;
};

D3D12SwapChain CreateSwapChain(D3D12Core& core, const D3D12SwapChainDesc& desc);

} // namespace D3D12CoreLib
