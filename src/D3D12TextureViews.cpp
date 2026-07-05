#include "D3D12Processing/D3D12TextureViews.hpp"
#include "D3D12Core/D3D12FormatUtil.hpp"

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

void ValidateTexture(const D3D12Resource& texture, const char* functionName) {
    if (!texture.Get()) {
        std::ostringstream os;
        os << functionName << ": null texture";
        throw ValidationError(os.str());
    }
    const auto desc = texture.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        std::ostringstream os;
        os << functionName << ": resource is not Texture2D";
        throw ValidationError(os.str());
    }
    if (desc.Width == 0 || desc.Height == 0) {
        std::ostringstream os;
        os << functionName << ": texture has zero size";
        throw ValidationError(os.str());
    }
}

void ValidateUavFlag(const D3D12Resource& texture, const char* functionName) {
    const auto desc = texture.GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        std::ostringstream os;
        os << functionName << ": texture was not created with ALLOW_UNORDERED_ACCESS";
        throw ValidationError(os.str());
    }
}

DXGI_FORMAT ResolveViewFormat(const D3D12Resource& texture, DXGI_FORMAT viewFormat) {
    return viewFormat == DXGI_FORMAT_UNKNOWN ? texture.GetFormat() : viewFormat;
}

} // namespace

D3D12TextureViewSet CreateRgbaTextureViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav,
    DXGI_FORMAT viewFormat) {

    constexpr const char* fn = "CreateRgbaTextureViewSet";
    ValidateTexture(texture, fn);
    if (!createSrv && !createUav) {
        throw ValidationError("CreateRgbaTextureViewSet: no view requested");
    }

    const DXGI_FORMAT format = ResolveViewFormat(texture, viewFormat);
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("CreateRgbaTextureViewSet: only R8G8B8A8_UNORM and B8G8R8A8_UNORM are supported");
    }
    if (createUav) {
        ValidateUavFlag(texture, fn);
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !context.SupportsRgba8Uav()) {
            throw UnsupportedFeatureError("CreateRgbaTextureViewSet: R8G8B8A8 UAV typed store is not supported");
        }
        if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !context.SupportsBgra8Uav()) {
            throw UnsupportedFeatureError("CreateRgbaTextureViewSet: B8G8R8A8 UAV typed store is not supported");
        }
    }

    D3D12TextureViewSet views;
    const UINT count = (createSrv ? 1u : 0u) + (createUav ? 1u : 0u);
    views.range = context.CbvSrvUavAllocator().AllocateRange(count);
    if (!views.range.shaderVisible) {
        throw ValidationError("CreateRgbaTextureViewSet: descriptor allocator must be shader-visible");
    }

    UINT index = 0;
    if (createSrv) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        CreateSrv(context.Core(), texture.Get(), srv, views.range.Cpu(index));
        views.srvIndex = index++;
    }
    if (createUav) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = 0;
        uav.Texture2D.PlaneSlice = 0;
        CreateUav(context.Core(), texture.Get(), uav, views.range.Cpu(index));
        views.uavIndex = index++;
    }
    return views;
}

D3D12TextureViewSet CreateNv12SrvViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture) {
    return CreateNv12SrvUavViewSet(context, texture, true, false);
}

D3D12TextureViewSet CreateNv12UavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture) {
    return CreateNv12SrvUavViewSet(context, texture, false, true);
}

D3D12TextureViewSet CreateNv12SrvUavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav) {

    constexpr const char* fn = "CreateNv12SrvUavViewSet";
    ValidateTexture(texture, fn);
    if (!createSrv && !createUav) {
        throw ValidationError("CreateNv12SrvUavViewSet: no view requested");
    }

    const auto desc = texture.GetDesc();
    if (desc.Format != DXGI_FORMAT_NV12) {
        throw UnsupportedFormatError("CreateNv12SrvUavViewSet: texture format must be NV12");
    }
    ValidateEvenSize(static_cast<UINT>(desc.Width), desc.Height, desc.Format, fn);

    if (createSrv && !context.SupportsNv12Srv()) {
        throw UnsupportedFeatureError("CreateNv12SrvUavViewSet: NV12 SRV plane views are not supported");
    }
    if (createUav) {
        ValidateUavFlag(texture, fn);
        if (!context.SupportsNv12Uav()) {
            throw UnsupportedFeatureError("CreateNv12SrvUavViewSet: NV12 UAV plane views are not supported");
        }
    }

    D3D12TextureViewSet views;
    const UINT count = (createSrv ? 2u : 0u) + (createUav ? 2u : 0u);
    views.range = context.CbvSrvUavAllocator().AllocateRange(count);
    if (!views.range.shaderVisible) {
        throw ValidationError("CreateNv12SrvUavViewSet: descriptor allocator must be shader-visible");
    }

    UINT index = 0;
    if (createSrv) {
        D3D12_SHADER_RESOURCE_VIEW_DESC y = {};
        y.Format = DXGI_FORMAT_R8_UNORM;
        y.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        y.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        y.Texture2D.MostDetailedMip = 0;
        y.Texture2D.MipLevels = 1;
        y.Texture2D.PlaneSlice = 0;
        CreateSrv(context.Core(), texture.Get(), y, views.range.Cpu(index));
        views.ySrvIndex = index++;

        D3D12_SHADER_RESOURCE_VIEW_DESC uv = y;
        uv.Format = DXGI_FORMAT_R8G8_UNORM;
        uv.Texture2D.PlaneSlice = 1;
        CreateSrv(context.Core(), texture.Get(), uv, views.range.Cpu(index));
        views.uvSrvIndex = index++;
    }
    if (createUav) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC y = {};
        y.Format = DXGI_FORMAT_R8_UNORM;
        y.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        y.Texture2D.MipSlice = 0;
        y.Texture2D.PlaneSlice = 0;
        CreateUav(context.Core(), texture.Get(), y, views.range.Cpu(index));
        views.yUavIndex = index++;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = y;
        uv.Format = DXGI_FORMAT_R8G8_UNORM;
        uv.Texture2D.PlaneSlice = 1;
        CreateUav(context.Core(), texture.Get(), uv, views.range.Cpu(index));
        views.uvUavIndex = index++;
    }
    return views;
}

} // namespace Processing
} // namespace D3D12CoreLib
