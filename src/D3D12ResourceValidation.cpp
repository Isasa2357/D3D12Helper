//
// D3D12ResourceValidation.cpp
//
#include <D3D12Helper/D3D12Gpu/D3D12ResourceValidation.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {
namespace {

void AddIssue(
    D3D12ValidationResult& result,
    D3D12ValidationIssueCode code,
    std::string message) {
    result.issues.push_back(D3D12ValidationIssue{ code, std::move(message) });
}

std::string FormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN: return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R8_UNORM: return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8G8_UNORM: return "DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_R16_UNORM: return "DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16G16_UNORM: return "DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R32G32_FLOAT: return "DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "DXGI_FORMAT_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_D32_FLOAT: return "DXGI_FORMAT_D32_FLOAT";
    case DXGI_FORMAT_NV12: return "DXGI_FORMAT_NV12";
    case DXGI_FORMAT_P010: return "DXGI_FORMAT_P010";
    default:
        return "DXGI_FORMAT(" + std::to_string(static_cast<unsigned int>(format)) + ")";
    }
}

std::string FlagsHex(D3D12_RESOURCE_FLAGS flags) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase
       << static_cast<unsigned int>(flags);
    return os.str();
}

D3D12ValidationResult ValidateTexture2DImpl(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement) {

    D3D12ValidationResult result;

    if (requirement.widthMultiple == 0) {
        AddIssue(result, D3D12ValidationIssueCode::InvalidRequirement,
                 "widthMultiple must be greater than zero");
    }
    if (requirement.heightMultiple == 0) {
        AddIssue(result, D3D12ValidationIssueCode::InvalidRequirement,
                 "heightMultiple must be greater than zero");
    }

    const UINT requiredFlags = static_cast<UINT>(requirement.requiredFlags);
    const UINT forbiddenFlags = static_cast<UINT>(requirement.forbiddenFlags);
    if ((requiredFlags & forbiddenFlags) != 0u) {
        AddIssue(result, D3D12ValidationIssueCode::InvalidRequirement,
                 "requiredFlags and forbiddenFlags overlap");
    }

    if (!resource) {
        AddIssue(result, D3D12ValidationIssueCode::NullResource,
                 "resource is null");
        return result;
    }

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    const bool isTexture2D =
        desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    if (!isTexture2D) {
        AddIssue(result, D3D12ValidationIssueCode::WrongDimension,
                 "resource dimension is not Texture2D");
    } else {
        if (requirement.width && desc.Width != *requirement.width) {
            AddIssue(result, D3D12ValidationIssueCode::WidthMismatch,
                "width mismatch: expected " + std::to_string(*requirement.width) +
                ", actual " + std::to_string(desc.Width));
        }
        if (requirement.height && desc.Height != *requirement.height) {
            AddIssue(result, D3D12ValidationIssueCode::HeightMismatch,
                "height mismatch: expected " + std::to_string(*requirement.height) +
                ", actual " + std::to_string(desc.Height));
        }
        if (requirement.format && desc.Format != *requirement.format) {
            AddIssue(result, D3D12ValidationIssueCode::FormatMismatch,
                "format mismatch: expected " + FormatName(*requirement.format) +
                ", actual " + FormatName(desc.Format));
        }
        if (requirement.arraySize && desc.DepthOrArraySize != *requirement.arraySize) {
            AddIssue(result, D3D12ValidationIssueCode::ArraySizeMismatch,
                "array size mismatch: expected " + std::to_string(*requirement.arraySize) +
                ", actual " + std::to_string(desc.DepthOrArraySize));
        }
        if (requirement.mipLevels && desc.MipLevels != *requirement.mipLevels) {
            AddIssue(result, D3D12ValidationIssueCode::MipLevelsMismatch,
                "mip levels mismatch: expected " + std::to_string(*requirement.mipLevels) +
                ", actual " + std::to_string(desc.MipLevels));
        }
        if (requirement.sampleCount && desc.SampleDesc.Count != *requirement.sampleCount) {
            AddIssue(result, D3D12ValidationIssueCode::SampleCountMismatch,
                "sample count mismatch: expected " + std::to_string(*requirement.sampleCount) +
                ", actual " + std::to_string(desc.SampleDesc.Count));
        }
        if (requirement.widthMultiple != 0 &&
            (desc.Width % requirement.widthMultiple) != 0) {
            AddIssue(result, D3D12ValidationIssueCode::WidthMultipleMismatch,
                "width " + std::to_string(desc.Width) +
                " is not a multiple of " + std::to_string(requirement.widthMultiple));
        }
        if (requirement.heightMultiple != 0 &&
            (desc.Height % requirement.heightMultiple) != 0) {
            AddIssue(result, D3D12ValidationIssueCode::HeightMultipleMismatch,
                "height " + std::to_string(desc.Height) +
                " is not a multiple of " + std::to_string(requirement.heightMultiple));
        }
    }

    const UINT actualFlags = static_cast<UINT>(desc.Flags);
    if ((actualFlags & requiredFlags) != requiredFlags) {
        AddIssue(result, D3D12ValidationIssueCode::MissingResourceFlags,
            "resource flags are missing required bits: required " +
            FlagsHex(requirement.requiredFlags) + ", actual " + FlagsHex(desc.Flags));
    }
    if ((actualFlags & forbiddenFlags) != 0u) {
        AddIssue(result, D3D12ValidationIssueCode::ForbiddenResourceFlags,
            "resource flags contain forbidden bits: forbidden " +
            FlagsHex(requirement.forbiddenFlags) + ", actual " + FlagsHex(desc.Flags));
    }

    if (requirement.expectedDevice) {
        ComPtr<ID3D12Device> actualDevice;
        const HRESULT hr = resource->GetDevice(IID_PPV_ARGS(&actualDevice));
        if (FAILED(hr) || !actualDevice) {
            AddIssue(result, D3D12ValidationIssueCode::DeviceQueryFailed,
                     "failed to query the resource device");
        } else if (actualDevice.Get() != requirement.expectedDevice) {
            AddIssue(result, D3D12ValidationIssueCode::DeviceMismatch,
                     "resource belongs to a different D3D12 device");
        }
    }

    return result;
}

void ThrowIfInvalid(
    const char* functionName,
    const D3D12ValidationResult& result) {
    if (!result.IsValid()) {
        throw std::runtime_error(std::string(functionName) + ": " + result.Message());
    }
}

} // namespace

std::string D3D12ValidationResult::Message() const {
    std::ostringstream os;
    for (size_t i = 0; i < issues.size(); ++i) {
        if (i != 0) os << "; ";
        os << issues[i].message;
    }
    return os.str();
}

D3D12ValidationResult ValidateTexture2D(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement) {
    return ValidateTexture2DImpl(resource, requirement);
}

void ValidateTexture2DOrThrow(
    ID3D12Resource* resource,
    const D3D12Texture2DRequirement& requirement) {
    ThrowIfInvalid(
        "ValidateTexture2DOrThrow",
        ValidateTexture2DImpl(resource, requirement));
}

D3D12ValidationResult ValidateTexture2DView(
    D3D12ResourceView resource,
    const D3D12Texture2DRequirement& requirement) {
    return ValidateTexture2DImpl(resource.Get(), requirement);
}

void ValidateTexture2DViewOrThrow(
    D3D12ResourceView resource,
    const D3D12Texture2DRequirement& requirement) {
    ThrowIfInvalid(
        "ValidateTexture2DViewOrThrow",
        ValidateTexture2DImpl(resource.Get(), requirement));
}

} // namespace D3D12CoreLib
