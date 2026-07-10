#pragma once
//
// D3D12PyramidRegionBlur.hpp
// Region-masked fast blur built from D3D12PyramidBlur + original/blurred mask blend.
//
#include <D3D12Helper/D3D12Processing/D3D12PyramidBlur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

struct D3D12PyramidRegionBlurWorkspace {
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    UINT levels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    D3D12PyramidBlurWorkspace blurWorkspace;
    D3D12Resource blurred;
};

struct D3D12PyramidRegionBlurWorkspaceView {
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    UINT levels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    D3D12PyramidBlurWorkspaceView blurWorkspace;
    D3D12ResourceView blurred;

    D3D12PyramidRegionBlurWorkspaceView() = default;

    explicit D3D12PyramidRegionBlurWorkspaceView(
        const D3D12PyramidRegionBlurWorkspace& workspace)
        : sourceWidth(workspace.sourceWidth),
          sourceHeight(workspace.sourceHeight),
          levels(workspace.levels),
          format(workspace.format),
          blurWorkspace(workspace.blurWorkspace),
          blurred(workspace.blurred) {}
};

struct D3D12PyramidRegionBlurStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12PyramidBlurWorkspaceStateDesc blurWorkspace;
    D3D12_RESOURCE_STATES blurredBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurredAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

class D3D12PyramidRegionBlur {
public:
    D3D12PyramidRegionBlur();
    ~D3D12PyramidRegionBlur();

    D3D12PyramidRegionBlur(const D3D12PyramidRegionBlur&) = delete;
    D3D12PyramidRegionBlur& operator=(const D3D12PyramidRegionBlur&) = delete;
    D3D12PyramidRegionBlur(D3D12PyramidRegionBlur&&) noexcept;
    D3D12PyramidRegionBlur& operator=(D3D12PyramidRegionBlur&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    D3D12PyramidRegionBlurWorkspace CreateWorkspace(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT levels,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    void RecordPyramidRegionBlur(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12PyramidRegionBlurWorkspace& workspace,
        D3D12Resource& dst,
        const PyramidRegionBlurDesc& desc);

    void RecordPyramidRegionBlurView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        const D3D12PyramidRegionBlurWorkspaceView& workspace,
        D3D12ResourceView dst,
        const PyramidRegionBlurDesc& desc,
        const D3D12PyramidRegionBlurStateDesc& state);

private:
    struct Pipelines;

    void EnsureInitialized() const;
    void EnsurePipelines();

    D3D12ProcessingContext* m_context = nullptr;
    D3D12ProcessingShaderCache m_shaderCache;
    D3D12PyramidBlur m_pyramidBlur;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
