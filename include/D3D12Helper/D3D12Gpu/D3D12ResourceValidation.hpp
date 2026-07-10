#pragma once
//
// D3D12ResourceValidation.hpp
// Usage-independent validation for externally supplied D3D12 resources.
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

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

    // nullptr disables device identity validation. This pointer is borrowed.
    ID3D12Device* expectedDevice = nullptr;

    // 1 disables the corresponding divisibility constraint.
    UINT64 widthMultiple = 1;
    UINT heightMultiple = 1;
};

D3D12ValidationResult ValidateTexture2D(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement);

void ValidateTexture2DOrThrow(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement);

} // namespace D3D12CoreLib
