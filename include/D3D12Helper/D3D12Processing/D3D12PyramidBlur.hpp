#pragma once
//
// D3D12PyramidBlur.hpp
// Fast approximate blur using downsample -> low-res blur -> upsample.
//
#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidProcessor.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>

#include <vector>

namespace D3D12CoreLib {
namespace Processing {

struct D3D12PyramidBlurWorkspace {
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    UINT levels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    std::vector<D3D12Resource> downTextures;
    D3D12Resource blurScratch;
    D3D12Resource blurredLow;
    std::vector<D3D12Resource> upTextures;
};

// Non-owning counterpart of D3D12PyramidBlurWorkspace. Every referenced
// resource must remain alive until submitted GPU work has completed.
struct D3D12PyramidBlurWorkspaceView {
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    UINT levels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    std::vector<D3D12ResourceView> downTextures;
    D3D12ResourceView blurScratch;
    D3D12ResourceView blurredLow;
    std::vector<D3D12ResourceView> upTextures;

    D3D12PyramidBlurWorkspaceView() = default;

    explicit D3D12PyramidBlurWorkspaceView(
        const D3D12PyramidBlurWorkspace& workspace)
        : sourceWidth(workspace.sourceWidth),
          sourceHeight(workspace.sourceHeight),
          levels(workspace.levels),
          format(workspace.format),
          blurScratch(workspace.blurScratch),
          blurredLow(workspace.blurredLow) {

        downTextures.reserve(workspace.downTextures.size());
        for (const auto& resource : workspace.downTextures) {
            downTextures.emplace_back(resource);
        }

        upTextures.reserve(workspace.upTextures.size());
        for (const auto& resource : workspace.upTextures) {
            upTextures.emplace_back(resource);
        }
    }
};

struct D3D12PyramidBlurWorkspaceStateDesc {
    std::vector<D3D12_RESOURCE_STATES> downBefore;
    std::vector<D3D12_RESOURCE_STATES> downAfter;

    D3D12_RESOURCE_STATES blurScratchBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurScratchAfter = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurredLowBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES blurredLowAfter = D3D12_RESOURCE_STATE_COMMON;

    std::vector<D3D12_RESOURCE_STATES> upBefore;
    std::vector<D3D12_RESOURCE_STATES> upAfter;
};

struct D3D12PyramidBlurStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter = D3D12_RESOURCE_STATE_COMMON;

    D3D12PyramidBlurWorkspaceStateDesc workspace;
    bool useExplicitStates = false;
};

class D3D12PyramidBlur {
public:
    static constexpr UINT MaxLevels = 6;

    D3D12PyramidBlur();
    ~D3D12PyramidBlur();

    D3D12PyramidBlur(const D3D12PyramidBlur&) = delete;
    D3D12PyramidBlur& operator=(const D3D12PyramidBlur&) = delete;
    D3D12PyramidBlur(D3D12PyramidBlur&&) noexcept;
    D3D12PyramidBlur& operator=(D3D12PyramidBlur&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    D3D12PyramidBlurWorkspace CreateWorkspace(
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

    void RecordPyramidBlur(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12PyramidBlurWorkspace& workspace,
        D3D12Resource& dst,
        const PyramidBlurDesc& desc);

    void RecordPyramidBlurView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12PyramidBlurWorkspaceView workspace,
        D3D12ResourceView dst,
        const PyramidBlurDesc& desc,
        const D3D12PyramidBlurStateDesc& state);

private:
    void EnsureInitialized() const;

    D3D12ProcessingContext* m_context = nullptr;
    D3D12PyramidProcessor m_pyramid;
    D3D12Blurrer m_blurrer;
};

} // namespace Processing
} // namespace D3D12CoreLib
