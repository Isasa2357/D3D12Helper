#pragma once
//
// D3D12ProcessingTypes.hpp
// Common public types for the D3D12 Processing Layer.
//
#include "../D3D12Core/D3D12Common.hpp"

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

struct D3D12ProcessingStateDesc {
    D3D12_RESOURCE_STATES srcBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES srcAfter  = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstBefore = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES dstAfter  = D3D12_RESOURCE_STATE_COMMON;
    bool useExplicitStates = false;
};

struct D3D12ProcessingCaps {
    bool rgba8Uav = false;
    bool bgra8Uav = false;
    bool nv12Srv = false;
    bool nv12Uav = false;
    bool r8TypedUavLoad = false;
    bool r8TypedUavStore = false;
    bool rg8TypedUavLoad = false;
    bool rg8TypedUavStore = false;
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
bool IsSupportedProcessingFormat(DXGI_FORMAT format) noexcept;
bool IsSupportedRgbaOutputFormat(DXGI_FORMAT format) noexcept;

ProcessingRect ResolveRect(const ProcessingRect& rect, UINT fallbackWidth, UINT fallbackHeight);
void ValidateRectInside(const ProcessingRect& rect, UINT width, UINT height, const char* functionName, const char* argumentName);
void ValidateEvenSize(UINT width, UINT height, DXGI_FORMAT format, const char* functionName);

} // namespace Processing
} // namespace D3D12CoreLib
