#pragma once
//
// D3D12Remap.hpp
// GPU remap pass for RGBA-like D3D12 textures.
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

class D3D12Remapper {
public:
    D3D12Remapper();
    ~D3D12Remapper();

    D3D12Remapper(const D3D12Remapper&) = delete;
    D3D12Remapper& operator=(const D3D12Remapper&) = delete;
    D3D12Remapper(D3D12Remapper&&) noexcept;
    D3D12Remapper& operator=(D3D12Remapper&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordRemap(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& map,
        D3D12Resource& dst,
        const RemapDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state = {});

    // Non-owning counterpart for externally owned resources. Explicit before /
    // after states are mandatory and all resources must remain alive until the
    // submitted GPU work has completed.
    void RecordRemapView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView map,
        D3D12ResourceView dst,
        const RemapDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state);

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
