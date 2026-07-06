#pragma once
//
// D3D12ProcessingTypes.hpp
// Common public types for the D3D12 Processing Layer.
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace Processing {

struct ProcessingSize {
    UINT width = 0;
    UINT height = 0;
};

struct ProcessingRect {
    INT x = 0;
    INT y = 0;
    UINT width = 0;
    UINT height = 0;
};

enum class ProcessingFilter : UINT {
    Point = 0,
    Linear = 1,
};

enum class ProcessingColorMatrix : UINT {
    Identity = 0,
    BT601 = 1,
    BT709 = 2,
    BT2020 = 3,
};

enum class ProcessingColorRange : UINT {
    Full = 0,
    Limited = 1,
};

enum class ProcessingAlphaMode : UINT {
    Ignore = 0,
    Preserve = 1,
    Premultiplied = 2,
};

enum class RemapCoordinateMode : UINT {
    AbsolutePixels = 0,
    NormalizedZeroToOne = 1,
};

enum class RemapBorderMode : UINT {
    Clamp = 0,
    Constant = 1,
};

enum class CompositeBlendMode : UINT {
    Copy = 0,
    AlphaBlend = 1,
    PremultipliedAlpha = 2,
    Add = 3,
};

enum class BlurMode : UINT {
    Box = 0,
    Gaussian = 1,
};

enum class BlurEdgeMode : UINT {
    Clamp = 0,
    Constant = 1,
};

enum class RegionShape : UINT {
    Circle = 0,
    Rect = 1,
};

enum class RegionSelection : UINT {
    Inside = 0,
    Outside = 1,
};

enum class RegionEffectMode : UINT {
    Darken = 0,
    Tint = 1,
    Grayscale = 2,
    Highlight = 3,
    AlphaFade = 4,
    Vignette = 5,
};

enum class KernelFilterMode : UINT {
    Custom3x3 = 0,
    Sharpen = 1,
    EdgeDetect = 2,
};

enum class KernelEdgeMode : UINT {
    Clamp = 0,
    Constant = 1,
};

enum class MaskChannel : UINT {
    Red = 0,
    Green = 1,
    Blue = 2,
    Alpha = 3,
    Luma = 4,
};

enum class MaskApplyMode : UINT {
    ApplyAlpha = 0,
    MultiplyRgb = 1,
    MultiplyRgba = 2,
    ReplaceAlpha = 3,
};

enum class MaskCombineMode : UINT {
    Add = 0,
    Multiply = 1,
    Max = 2,
    Min = 3,
    Subtract = 4,
};

struct ProcessingColorDesc {
    ProcessingColorMatrix srcMatrix = ProcessingColorMatrix::BT709;
    ProcessingColorRange  srcRange  = ProcessingColorRange::Full;
    ProcessingColorMatrix dstMatrix = ProcessingColorMatrix::BT709;
    ProcessingColorRange  dstRange  = ProcessingColorRange::Full;
    ProcessingAlphaMode   alphaMode = ProcessingAlphaMode::Ignore;
};

struct FormatConvertDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingColorDesc color = {};
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
};

struct ResizeDesc {
    ProcessingFilter filter = ProcessingFilter::Linear;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
};

// 1 dispatch で format convert と resize を行うための desc。
// 現在は RGBA-like -> RGBA-like と YUV420(NV12/P010) -> RGBA-like を対象にする。
struct FusedConvertResizeDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingColorDesc color = {};
    ProcessingFilter filter = ProcessingFilter::Linear;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
};

struct RemapDesc {
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT mapFormat = DXGI_FORMAT_R32G32_FLOAT;
    ProcessingFilter filter = ProcessingFilter::Linear;
    ProcessingRect dstRect = {};
    RemapCoordinateMode coordinateMode = RemapCoordinateMode::AbsolutePixels;
    RemapBorderMode borderMode = RemapBorderMode::Clamp;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct CompositeDesc {
    DXGI_FORMAT baseFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT overlayFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dstFormat = DXGI_FORMAT_UNKNOWN;
    ProcessingRect baseRect = {};
    ProcessingRect overlayRect = {};
    ProcessingRect dstRect = {};
    CompositeBlendMode blendMode = CompositeBlendMode::AlphaBlend;
    float opacity = 1.0f;
};

struct BlurDesc {
    BlurMode mode = BlurMode::Gaussian;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    UINT radius = 5;
    float sigma = 2.0f;
    BlurEdgeMode edgeMode = BlurEdgeMode::Clamp;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct RegionEffectDesc {
    RegionShape shape = RegionShape::Circle;
    RegionSelection selection = RegionSelection::Outside;
    RegionEffectMode effect = RegionEffectMode::Darken;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};

    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float rectX = 0.0f;
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;
    float edgeSoftness = 0.0f;
    float strength = 1.0f;
    float tintColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct RegionBlurDesc {
    RegionShape shape = RegionShape::Circle;
    RegionSelection selection = RegionSelection::Outside;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};

    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float rectX = 0.0f;
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;
    float edgeSoftness = 0.0f;
    float blurStrength = 1.0f;

    BlurMode blurMode = BlurMode::Gaussian;
    UINT blurRadius = 5;
    float blurSigma = 2.0f;
    BlurEdgeMode blurEdgeMode = BlurEdgeMode::Clamp;
};

struct ColorAdjustDesc {
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    float brightness = 0.0f;
    float contrast = 1.0f;
    float gamma = 1.0f;
    float saturation = 1.0f;
    bool preserveAlpha = true;
};

struct KernelFilterDesc {
    KernelFilterMode mode = KernelFilterMode::Sharpen;
    KernelEdgeMode edgeMode = KernelEdgeMode::Clamp;
    ProcessingRect srcRect = {};
    ProcessingRect dstRect = {};
    float kernel[9] = {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
    };
    float scale = 1.0f;
    float bias = 0.0f;
    bool preserveAlpha = true;
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct MaskApplyDesc {
    ProcessingRect srcRect = {};
    ProcessingRect maskRect = {};
    ProcessingRect dstRect = {};
    MaskApplyMode mode = MaskApplyMode::ApplyAlpha;
    MaskChannel channel = MaskChannel::Alpha;
    bool invert = false;
    float strength = 1.0f;
};

struct MaskBlendDesc {
    ProcessingRect baseRect = {};
    ProcessingRect overlayRect = {};
    ProcessingRect maskRect = {};
    ProcessingRect dstRect = {};
    MaskChannel channel = MaskChannel::Alpha;
    bool invert = false;
    float opacity = 1.0f;
};

struct MaskCombineDesc {
    ProcessingRect maskARect = {};
    ProcessingRect maskBRect = {};
    ProcessingRect dstRect = {};
    MaskCombineMode mode = MaskCombineMode::Max;
    MaskChannel channelA = MaskChannel::Alpha;
    MaskChannel channelB = MaskChannel::Alpha;
    bool invertA = false;
    bool invertB = false;
    float scale = 1.0f;
    float bias = 0.0f;
};

struct MaskInvertDesc {
    ProcessingRect maskRect = {};
    ProcessingRect dstRect = {};
    MaskChannel channel = MaskChannel::Alpha;
};

struct D3D12ProcessingStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter  = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

struct D3D12ProcessingTwoInputStateDesc {
    D3D12_RESOURCE_STATES src0Before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src0After  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src1Before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src1After  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter   = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

struct D3D12ProcessingThreeInputStateDesc {
    D3D12_RESOURCE_STATES src0Before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src0After  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src1Before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src1After  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src2Before = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES src2After  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter   = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

struct D3D12ProcessingBlurStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES scratchBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES scratchAfter  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter  = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

struct D3D12ProcessingCaps {
    bool rgba8Uav = false;
    bool bgra8Uav = false;
    bool rgba16FloatUav = false;

    bool nv12Srv = false;
    bool nv12Uav = false;
    bool p010Srv = false;
    bool p010Uav = false;

    bool r8TypedUavLoad = false;
    bool r8TypedUavStore = false;
    bool rg8TypedUavLoad = false;
    bool rg8TypedUavStore = false;
    bool r16TypedUavLoad = false;
    bool r16TypedUavStore = false;
    bool rg16TypedUavLoad = false;
    bool rg16TypedUavStore = false;
};

class ProcessingError : public std::runtime_error {
public:
    explicit ProcessingError(const std::string& message) : std::runtime_error(message) {}
};

class UnsupportedFormatError : public ProcessingError {
public:
    explicit UnsupportedFormatError(const std::string& message) : ProcessingError(message) {}
};

class UnsupportedFeatureError : public ProcessingError {
public:
    explicit UnsupportedFeatureError(const std::string& message) : ProcessingError(message) {}
};

class ValidationError : public ProcessingError {
public:
    explicit ValidationError(const std::string& message) : ProcessingError(message) {}
};

bool IsRgbaLikeFormat(DXGI_FORMAT format) noexcept;
bool IsYuv420Format(DXGI_FORMAT format) noexcept;
bool IsSupportedProcessingFormat(DXGI_FORMAT format) noexcept;
bool IsSupportedRgbaOutputFormat(DXGI_FORMAT format) noexcept;
bool IsSupportedRemapMapFormat(DXGI_FORMAT format) noexcept;
bool IsSupportedCompositeFormat(DXGI_FORMAT format) noexcept;

ProcessingRect ResolveRect(const ProcessingRect& rect, UINT fallbackWidth, UINT fallbackHeight);
void ValidateRectInside(const ProcessingRect& rect, UINT width, UINT height, const char* functionName, const char* argumentName);
void ValidateEvenSize(UINT width, UINT height, DXGI_FORMAT format, const char* functionName);
void ValidateYuv420Rect(const ProcessingRect& rect, const char* functionName, const char* argumentName);
void ValidateOpacity(float opacity, const char* functionName);

} // namespace Processing
} // namespace D3D12CoreLib
