#pragma once
//
// D3D12ResourceValidation.hpp
// Usage-independent validation for externally supplied D3D12 resources.
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>

#include <optional>
#include <string>
#include <vector>

namespace D3D12CoreLib {

enum class D3D12ValidationIssueCode {
    InvalidRequirement,
    NullResource,
    WrongDimension,
    WidthMismatch,
    HeightMismatch,
    FormatMismatch,
    ArraySizeMismatch,
    MipLevelsMismatch,
    SampleCountMismatch,
    MissingResourceFlags,
    ForbiddenResourceFlags,
    WidthMultipleMismatch,
    HeightMultipleMismatch,
    DeviceQueryFailed,
    DeviceMismatch,
};

struct D3D12ValidationIssue {
    D3D12ValidationIssueCode code = D3D12ValidationIssueCode::InvalidRequirement;
    std::string message;
};

struct D3D12ValidationResult {
    std::vector<D3D12ValidationIssue> issues;

    bool IsValid() const noexcept { return issues.empty(); }
    explicit operator bool() const noexcept { return IsValid(); }

    // Joins all issue messages in validation order. Empty when valid.
    std::string Message() const;
};

struct D3D12Texture2DRequirement {
    std::optional<UINT64> width;
    std::optional<UINT> height;
    std::optional<DXGI_FORMAT> format;
    std::optional<UINT16> arraySize;
    std::optional<UINT16> mipLevels;
    std::optional<UINT> sampleCount;

    D3D12_RESOURCE_FLAGS requiredFlags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_FLAGS forbiddenFlags = D3D12_RESOURCE_FLAG_NONE;

    // nullptr disables device identity validation. This pointer is borrowed and
    // must remain valid for the duration of validation.
    ID3D12Device* expectedDevice = nullptr;

    // 1 disables the corresponding divisibility constraint. Zero is an invalid
    // requirement and is reported through D3D12ValidationResult.
    UINT64 widthMultiple = 1;
    UINT heightMultiple = 1;
};

// Existing raw-pointer API added in Phase 1.
D3D12ValidationResult ValidateTexture2D(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement);

void ValidateTexture2DOrThrow(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement);

// Distinct names avoid introducing overload ambiguity into the Phase 1 API.
// The view remains non-owning for the complete validation call.
D3D12ValidationResult ValidateTexture2DView(
    D3D12ResourceView resource,
    const D3D12Texture2DRequirement& requirement);

void ValidateTexture2DViewOrThrow(
    D3D12ResourceView resource,
    const D3D12Texture2DRequirement& requirement);

} // namespace D3D12CoreLib
