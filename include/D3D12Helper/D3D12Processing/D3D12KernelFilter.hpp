#pragma once
//
// D3D12KernelFilter.hpp
// Single-pass 3x3 convolution filter for RGBA-like textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12KernelFilter {
public:
    D3D12KernelFilter();
    ~D3D12KernelFilter();

    D3D12KernelFilter(const D3D12KernelFilter&) = delete;
    D3D12KernelFilter& operator=(const D3D12KernelFilter&) = delete;
    D3D12KernelFilter(D3D12KernelFilter&&) noexcept;
    D3D12KernelFilter& operator=(D3D12KernelFilter&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordKernelFilter(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const KernelFilterDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

private:
    struct Pipelines;

    void EnsureInitialized() const;
    void EnsurePipelines();

    D3D12ProcessingContext* m_context = nullptr;
    D3D12ProcessingShaderCache m_shaderCache;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
