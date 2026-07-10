#pragma once
//
// D3D12Resize.hpp
// GPU resize pass for RGBA-like D3D12 textures.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12Resizer {
public:
    D3D12Resizer();
    ~D3D12Resizer();

    D3D12Resizer(const D3D12Resizer&) = delete;
    D3D12Resizer& operator=(const D3D12Resizer&) = delete;
    D3D12Resizer(D3D12Resizer&&) noexcept;
    D3D12Resizer& operator=(D3D12Resizer&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordResize(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const ResizeDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    // Non-owning counterpart for externally owned resources. Explicit before /
    // after states are mandatory and the resources must remain alive until the
    // submitted GPU work has completed.
    void RecordResizeView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const ResizeDesc& desc,
        const D3D12ProcessingStateDesc& state);

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
