#pragma once
//
// D3D12PyramidBlur.hpp
// Fast approximate blur using downsample -> low-res blur -> upsample.
//
#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidProcessor.hpp>

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

private:
    void EnsureInitialized() const;

    D3D12ProcessingContext* m_context = nullptr;
    D3D12PyramidProcessor m_pyramid;
    D3D12Blurrer m_blurrer;
};

} // namespace Processing
} // namespace D3D12CoreLib
