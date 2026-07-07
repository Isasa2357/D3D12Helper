#pragma once
//
// D3D12CpuImage.hpp - CPU-side image buffer for D3D12 Texture2D transfer.
//
#include <D3D12Helper/D3D12Foundation/D3D12Common.hpp>

#include <cstdint>
#include <vector>

namespace D3D12CoreLib {

struct D3D12CpuImagePlane {
    UINT width = 0;
    UINT height = 0;
    UINT rowPitch = 0;
    UINT64 offsetBytes = 0;
};

struct D3D12CpuImage {
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    std::vector<D3D12CpuImagePlane> planes;
    std::vector<uint8_t> pixels;

    bool Empty() const noexcept;
    UINT PlaneCount() const noexcept;
    UINT64 SizeBytes() const noexcept;
};

bool IsSinglePlaneCpuImageFormat(DXGI_FORMAT format) noexcept;

UINT GetPackedRowPitch(UINT width, DXGI_FORMAT format);
UINT64 GetRequiredCpuImageSize(UINT width, UINT height, DXGI_FORMAT format, UINT rowPitch = 0);

D3D12CpuImage CreateCpuImage(UINT width, UINT height, DXGI_FORMAT format, UINT rowPitch = 0);
void ValidateCpuImage(const D3D12CpuImage& image, const char* functionName);

void CopyRows(void* dst, UINT dstRowPitch, const void* src, UINT srcRowPitch, UINT rowBytes, UINT height);
std::vector<uint8_t> PackRows(const void* src, UINT srcRowPitch, UINT rowBytes, UINT height);
void UnpackRows(void* dst, UINT dstRowPitch, const void* packed, UINT rowBytes, UINT height);

} // namespace D3D12CoreLib
