#pragma once
//
// D3D12AdvancedProcessing.hpp
// Advanced Layer 3 processing passes: affine/perspective transform, 3D LUT,
// and undistort-map application.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Remap.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

// dstToSrc maps destination-local pixel coordinates to source-local pixel
// coordinates:
//   sx = m00 * x + m01 * y + m02
//   sy = m10 * x + m11 * y + m12
struct AffineTransformDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingFilter filter = ProcessingFilter::Linear;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    float dstToSrc[6] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    RemapBorderMode borderMode = RemapBorderMode::Clamp;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// dstToSrc is a row-major 3x3 homography mapping destination-local pixel
// coordinates to source-local pixel coordinates. The shader divides by the
// third row value when it is non-zero.
struct PerspectiveTransformDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingFilter filter = ProcessingFilter::Linear;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    float dstToSrc[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };
    RemapBorderMode borderMode = RemapBorderMode::Clamp;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// 3D LUT pass. The LUT resource must be a Texture3D whose RGB input domain is
// [0, 1]. When lutWidth/Height/Depth are zero, the dimensions are read from the
// resource description. The shader performs manual trilinear interpolation.
struct Lut3DDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT lutFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    UINT lutWidth = 0;
    UINT lutHeight = 0;
    UINT lutDepth = 0;
    float strength = 1.0f;
    bool preserveAlpha = true;
};

class D3D12AdvancedProcessor {
public:
    D3D12AdvancedProcessor();
    ~D3D12AdvancedProcessor();

    D3D12AdvancedProcessor(const D3D12AdvancedProcessor&) = delete;
    D3D12AdvancedProcessor& operator=(const D3D12AdvancedProcessor&) = delete;
    D3D12AdvancedProcessor(D3D12AdvancedProcessor&&) noexcept;
    D3D12AdvancedProcessor& operator=(D3D12AdvancedProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    void RecordAffineTransform(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const AffineTransformDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordPerspectiveTransform(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const PerspectiveTransformDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    void RecordApplyLut3D(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& lut,
        D3D12Resource& dst,
        const Lut3DDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state = {});

    // Thin alias over D3D12Remapper for camera undistort/remap maps.
    // The map texture must match RemapDesc::mapFormat, currently R32G32_FLOAT.
    void RecordApplyUndistortMap(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& map,
        D3D12Resource& dst,
        const RemapDesc& desc,
        const D3D12ProcessingTwoInputStateDesc& state = {});

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
    D3D12Remapper m_remapper;
};

} // namespace Processing
} // namespace D3D12CoreLib
