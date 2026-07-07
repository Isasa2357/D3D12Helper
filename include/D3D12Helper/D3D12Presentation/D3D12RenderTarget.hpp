#pragma once
//
// D3D12RenderTarget.hpp - offscreen render target helper.
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorAllocator.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

struct D3D12RenderTargetDesc {
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    FLOAT clearDepth = 1.0f;
    UINT8 clearStencil = 0;
};

class D3D12RenderTarget {
public:
    D3D12RenderTarget() = default;

    void Initialize(D3D12Core& core, const D3D12RenderTargetDesc& desc);
    void Reset() noexcept;

    bool IsValid() const noexcept { return m_color.Get() != nullptr; }
    UINT Width() const noexcept { return m_desc.width; }
    UINT Height() const noexcept { return m_desc.height; }
    DXGI_FORMAT ColorFormat() const noexcept { return m_desc.colorFormat; }
    DXGI_FORMAT DepthFormat() const noexcept { return m_desc.depthFormat; }

    D3D12Resource& ColorResource() noexcept { return m_color; }
    const D3D12Resource& ColorResource() const noexcept { return m_color; }
    D3D12Resource& DepthResource() noexcept { return m_depth; }
    const D3D12Resource& DepthResource() const noexcept { return m_depth; }

    D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const noexcept { return m_rtv.cpu; }
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const noexcept { return m_dsv.cpu; }
    D3D12_VIEWPORT Viewport() const noexcept;
    D3D12_RECT ScissorRect() const noexcept;

    void Bind(ID3D12GraphicsCommandList* commandList) const;
    void Clear(ID3D12GraphicsCommandList* commandList) const;
    void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList) const;

private:
    D3D12RenderTargetDesc m_desc;
    D3D12Resource m_color;
    D3D12Resource m_depth;
    D3D12DescriptorAllocator m_rtvAllocator;
    D3D12DescriptorAllocator m_dsvAllocator;
    D3D12DescriptorHandle m_rtv;
    D3D12DescriptorHandle m_dsv;
};

D3D12RenderTarget CreateRenderTarget(D3D12Core& core, const D3D12RenderTargetDesc& desc);
D3D12_VIEWPORT MakeViewport(UINT width, UINT height, FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f);
D3D12_RECT MakeScissorRect(UINT width, UINT height);

} // namespace D3D12CoreLib
