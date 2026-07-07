#include <D3D12Helper/D3D12Gpu/D3D12CpuImage.hpp>
#include <D3D12Helper/D3D12Foundation/D3D12FormatUtil.hpp>

#include <cstring>
#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace {
[[noreturn]] void Err(const char* fn, const char* msg) {
    throw std::invalid_argument(std::string(fn ? fn : "D3D12CpuImage") + ": " + msg);
}
void CheckDim(UINT w, UINT h, const char* fn) {
    if (w == 0) Err(fn, "width must be > 0");
    if (h == 0) Err(fn, "height must be > 0");
}
}

bool D3D12CpuImage::Empty() const noexcept {
    return width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN || planes.empty() || pixels.empty();
}

UINT D3D12CpuImage::PlaneCount() const noexcept {
    return static_cast<UINT>(planes.size());
}

UINT64 D3D12CpuImage::SizeBytes() const noexcept {
    return static_cast<UINT64>(pixels.size());
}

bool IsSinglePlaneCpuImageFormat(DXGI_FORMAT format) noexcept {
    if (format == DXGI_FORMAT_UNKNOWN) return false;
    if (FormatUtil::IsTypelessFormat(format) || FormatUtil::IsDepthFormat(format) ||
        FormatUtil::IsBlockCompressedFormat(format) || FormatUtil::IsPlanarFormat(format)) {
        return false;
    }
    return FormatUtil::BytesPerPixel(format) != 0;
}

UINT GetPackedRowPitch(UINT width, DXGI_FORMAT format) {
    if (width == 0) Err("GetPackedRowPitch", "width must be > 0");
    if (!IsSinglePlaneCpuImageFormat(format)) Err("GetPackedRowPitch", "unsupported format");
    return width * FormatUtil::BytesPerPixel(format);
}

UINT64 GetRequiredCpuImageSize(UINT width, UINT height, DXGI_FORMAT format, UINT rowPitch) {
    CheckDim(width, height, "GetRequiredCpuImageSize");
    const UINT packed = GetPackedRowPitch(width, format);
    const UINT pitch = rowPitch ? rowPitch : packed;
    if (pitch < packed) Err("GetRequiredCpuImageSize", "row pitch is too small");
    return static_cast<UINT64>(pitch) * height;
}

D3D12CpuImage CreateCpuImage(UINT width, UINT height, DXGI_FORMAT format, UINT rowPitch) {
    CheckDim(width, height, "CreateCpuImage");
    const UINT packed = GetPackedRowPitch(width, format);
    const UINT pitch = rowPitch ? rowPitch : packed;
    if (pitch < packed) Err("CreateCpuImage", "row pitch is too small");
    const UINT64 bytes = GetRequiredCpuImageSize(width, height, format, pitch);
    if (bytes > static_cast<UINT64>(static_cast<size_t>(-1))) Err("CreateCpuImage", "image is too large");
    D3D12CpuImage img;
    img.width = width;
    img.height = height;
    img.format = format;
    img.planes.push_back(D3D12CpuImagePlane{ width, height, pitch, 0 });
    img.pixels.resize(static_cast<size_t>(bytes), 0);
    return img;
}

void ValidateCpuImage(const D3D12CpuImage& image, const char* fn) {
    CheckDim(image.width, image.height, fn);
    if (!IsSinglePlaneCpuImageFormat(image.format)) Err(fn, "unsupported format");
    if (image.planes.size() != 1) Err(fn, "expected one plane");
    const auto& p = image.planes[0];
    if (p.width != image.width || p.height != image.height) Err(fn, "plane size mismatch");
    if (p.rowPitch < GetPackedRowPitch(image.width, image.format)) Err(fn, "row pitch is too small");
    const UINT64 required = p.offsetBytes + static_cast<UINT64>(p.rowPitch) * p.height;
    if (required > image.pixels.size()) Err(fn, "pixel buffer is too small");
}

void CopyRows(void* dst, UINT dstPitch, const void* src, UINT srcPitch, UINT rowBytes, UINT height) {
    if (height == 0 || rowBytes == 0) return;
    if (!dst) Err("CopyRows", "destination pointer is null");
    if (!src) Err("CopyRows", "source pointer is null");
    if (dstPitch < rowBytes) Err("CopyRows", "destination pitch is too small");
    if (srcPitch < rowBytes) Err("CopyRows", "source pitch is too small");
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(d + static_cast<size_t>(y) * dstPitch, s + static_cast<size_t>(y) * srcPitch, rowBytes);
    }
}

std::vector<uint8_t> PackRows(const void* src, UINT srcPitch, UINT rowBytes, UINT height) {
    if (height == 0 || rowBytes == 0) return {};
    if (!src) Err("PackRows", "source pointer is null");
    if (srcPitch < rowBytes) Err("PackRows", "source pitch is too small");
    std::vector<uint8_t> out(static_cast<size_t>(rowBytes) * height);
    CopyRows(out.data(), rowBytes, src, srcPitch, rowBytes, height);
    return out;
}

void UnpackRows(void* dst, UINT dstPitch, const void* packed, UINT rowBytes, UINT height) {
    if (height == 0 || rowBytes == 0) return;
    if (!packed) Err("UnpackRows", "source pointer is null");
    CopyRows(dst, dstPitch, packed, rowBytes, rowBytes, height);
}

} // namespace D3D12CoreLib
