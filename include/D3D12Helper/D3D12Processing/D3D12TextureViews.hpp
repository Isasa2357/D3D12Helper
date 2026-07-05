#pragma once
//
// D3D12TextureViews.hpp
// Processing-specific view sets built on Layer 2 full-desc view helpers.
//
#include "D3D12ProcessingContext.hpp"
#include "../D3D12Framework/D3D12Helpers.hpp"
#include "../D3D12Framework/D3D12Resource.hpp"

namespace D3D12CoreLib {
namespace Processing {

struct D3D12TextureViewSet {
    D3D12DescriptorRange range = {};

    UINT srvIndex = UINT_MAX;
    UINT uavIndex = UINT_MAX;
    UINT ySrvIndex = UINT_MAX;
    UINT uvSrvIndex = UINT_MAX;
    UINT yUavIndex = UINT_MAX;
    UINT uvUavIndex = UINT_MAX;

    bool HasSrv() const noexcept { return srvIndex != UINT_MAX; }
    bool HasUav() const noexcept { return uavIndex != UINT_MAX; }
    bool HasYuv420Srv() const noexcept { return ySrvIndex != UINT_MAX && uvSrvIndex != UINT_MAX; }
    bool HasYuv420Uav() const noexcept { return yUavIndex != UINT_MAX && uvUavIndex != UINT_MAX; }
    bool HasNv12Srv() const noexcept { return HasYuv420Srv(); }
    bool HasNv12Uav() const noexcept { return HasYuv420Uav(); }

    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const noexcept { return range.Cpu(index); }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const noexcept { return range.Gpu(index); }
};

D3D12TextureViewSet CreateRgbaTextureViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav,
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN);

// NV12 専用の互換 API。
D3D12TextureViewSet CreateNv12SrvViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture);

D3D12TextureViewSet CreateNv12UavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture);

D3D12TextureViewSet CreateNv12SrvUavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav);

// YUV420 2-plane format（NV12 / P010）用。
D3D12TextureViewSet CreateYuv420SrvViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture);

D3D12TextureViewSet CreateYuv420UavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture);

D3D12TextureViewSet CreateYuv420SrvUavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav);

} // namespace Processing
} // namespace D3D12CoreLib
