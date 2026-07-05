#include "D3D12Processing/D3D12ProcessingTypes.hpp"
#include "D3D12Core/D3D12FormatUtil.hpp"

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {

bool IsRgbaLikeFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM;
}

bool IsSupportedProcessingFormat(DXGI_FORMAT format) noexcept {
    return IsRgbaLikeFormat(format) || format == DXGI_FORMAT_NV12;
}

bool IsSupportedRgbaOutputFormat(DXGI_FORMAT format) noexcept {
    return IsRgbaLikeFormat(format);
}

ProcessingRect ResolveRect(const ProcessingRect& rect, UINT fallbackWidth, UINT fallbackHeight) {
    ProcessingRect r = rect;
    if (r.width == 0) {
        if (r.x < 0 || static_cast<UINT>(r.x) > fallbackWidth) {
            throw ValidationError("ResolveRect: x is outside fallback width");
        }
        r.width = fallbackWidth - static_cast<UINT>(r.x);
    }
    if (r.height == 0) {
        if (r.y < 0 || static_cast<UINT>(r.y) > fallbackHeight) {
            throw ValidationError("ResolveRect: y is outside fallback height");
        }
        r.height = fallbackHeight - static_cast<UINT>(r.y);
    }
    return r;
}

void ValidateRectInside(const ProcessingRect& rect, UINT width, UINT height, const char* functionName, const char* argumentName) {
    if (rect.width == 0 || rect.height == 0) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " has zero size";
        throw ValidationError(os.str());
    }
    if (rect.x < 0 || rect.y < 0) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " has negative origin";
        throw ValidationError(os.str());
    }
    const UINT x = static_cast<UINT>(rect.x);
    const UINT y = static_cast<UINT>(rect.y);
    if (x > width || y > height || rect.width > width - x || rect.height > height - y) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is outside resource bounds";
        throw ValidationError(os.str());
    }
}

void ValidateEvenSize(UINT width, UINT height, DXGI_FORMAT format, const char* functionName) {
    if (FormatUtil::RequiresEvenSize(format) && ((width & 1u) != 0u || (height & 1u) != 0u)) {
        std::ostringstream os;
        os << functionName << ": format requires even width and height";
        throw ValidationError(os.str());
    }
}

} // namespace Processing
} // namespace D3D12CoreLib
