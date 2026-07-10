#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12FormatUtil.hpp>

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

void ValidateTexture(D3D12ResourceView texture, const char* functionName) {
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

void ValidateUavFlag(D3D12ResourceView texture, const char* functionName) {
    const auto desc = texture.GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        std::ostringstream os;
        os << functionName << ": texture was not created with ALLOW_UNORDERED_ACCESS";
        throw ValidationError(os.str());
    }
}

DXGI_FORMAT ResolveViewFormat(D3D12ResourceView texture, DXGI_FORMAT viewFormat) {
    return viewFormat == DXGI_FORMAT_UNKNOWN ? texture.GetFormat() : viewFormat;
}

void ValidateRgbaUavSupport(D3D12ProcessingContext& context, DXGI_FORMAT format, const char* functionName) {
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !context.SupportsRgba8Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !context.SupportsBgra8Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": B8G8R8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT && !context.SupportsRgba16FloatUav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": R16G16B16A16_FLOAT UAV typed store is not supported");
    }
}

struct PlaneViewFormats {
    DXGI_FORMAT y;
    DXGI_FORMAT uv;
};

PlaneViewFormats GetYuv420PlaneViewFormats(DXGI_FORMAT textureFormat, const char* functionName) {
    if (textureFormat == DXGI_FORMAT_NV12) {
        return { DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM };
    }
    if (textureFormat == DXGI_FORMAT_P010) {
        return { DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM };
    }
    throw UnsupportedFormatError(std::string(functionName) + ": texture format must be NV12 or P010");
}

void ValidateYuv420Caps(D3D12ProcessingContext& context, DXGI_FORMAT textureFormat, bool createSrv, bool createUav, const char* functionName) {
    if (textureFormat == DXGI_FORMAT_NV12) {
        if (createSrv && !context.SupportsNv12Srv()) {
            throw UnsupportedFeatureError(std::string(functionName) + ": NV12 SRV plane views are not supported");
        }
        if (createUav && !context.SupportsNv12Uav()) {
            throw UnsupportedFeatureError(std::string(functionName) + ": NV12 UAV plane views are not supported");
        }
        return;
    }
    if (textureFormat == DXGI_FORMAT_P010) {
        if (createSrv && !context.SupportsP010Srv()) {
            throw UnsupportedFeatureError(std::string(functionName) + ": P010 SRV plane views are not supported");
        }
        if (createUav && !context.SupportsP010Uav()) {
            throw UnsupportedFeatureError(std::string(functionName) + ": P010 UAV plane views are not supported");
        }
        return;
    }
    throw UnsupportedFormatError(std::string(functionName) + ": texture format must be NV12 or P010");
}

D3D12TextureViewSet CreateRgbaTextureViewSetImpl(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture,
    bool createSrv,
    bool createUav,
    DXGI_FORMAT viewFormat,
    const char* functionName) {

    ValidateTexture(texture, functionName);
    if (!createSrv && !createUav) {
        throw ValidationError(std::string(functionName) + ": no view requested");
    }

    const DXGI_FORMAT format = ResolveViewFormat(texture, viewFormat);
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError(std::string(functionName) +
            ": only R8G8B8A8_UNORM, B8G8R8A8_UNORM, and R16G16B16A16_FLOAT are supported");
    }
    if (createUav) {
        ValidateUavFlag(texture, functionName);
        ValidateRgbaUavSupport(context, format, functionName);
    }

    D3D12TextureViewSet views;
    const UINT count = (createSrv ? 1u : 0u) + (createUav ? 1u : 0u);
    views.range = context.CbvSrvUavAllocator().AllocateRange(count);
    if (!views.range.shaderVisible) {
        throw ValidationError(std::string(functionName) + ": descriptor allocator must be shader-visible");
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

D3D12TextureViewSet CreateYuv420SrvUavViewSetImpl(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture,
    bool createSrv,
    bool createUav,
    const char* functionName) {

    ValidateTexture(texture, functionName);
    if (!createSrv && !createUav) {
        throw ValidationError(std::string(functionName) + ": no view requested");
    }

    const auto desc = texture.GetDesc();
    const PlaneViewFormats planeFormats = GetYuv420PlaneViewFormats(desc.Format, functionName);
    ValidateEvenSize(static_cast<UINT>(desc.Width), desc.Height, desc.Format, functionName);
    ValidateYuv420Caps(context, desc.Format, createSrv, createUav, functionName);

    if (createUav) {
        ValidateUavFlag(texture, functionName);
    }

    D3D12TextureViewSet views;
    const UINT count = (createSrv ? 2u : 0u) + (createUav ? 2u : 0u);
    views.range = context.CbvSrvUavAllocator().AllocateRange(count);
    if (!views.range.shaderVisible) {
        throw ValidationError(std::string(functionName) + ": descriptor allocator must be shader-visible");
    }

    UINT index = 0;
    if (createSrv) {
        D3D12_SHADER_RESOURCE_VIEW_DESC y = {};
        y.Format = planeFormats.y;
        y.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        y.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        y.Texture2D.MostDetailedMip = 0;
        y.Texture2D.MipLevels = 1;
        y.Texture2D.PlaneSlice = 0;
        CreateSrv(context.Core(), texture.Get(), y, views.range.Cpu(index));
        views.ySrvIndex = index++;

        D3D12_SHADER_RESOURCE_VIEW_DESC uv = y;
        uv.Format = planeFormats.uv;
        uv.Texture2D.PlaneSlice = 1;
        CreateSrv(context.Core(), texture.Get(), uv, views.range.Cpu(index));
        views.uvSrvIndex = index++;
    }
    if (createUav) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC y = {};
        y.Format = planeFormats.y;
        y.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        y.Texture2D.MipSlice = 0;
        y.Texture2D.PlaneSlice = 0;
        CreateUav(context.Core(), texture.Get(), y, views.range.Cpu(index));
        views.yUavIndex = index++;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = y;
        uv.Format = planeFormats.uv;
        uv.Texture2D.PlaneSlice = 1;
        CreateUav(context.Core(), texture.Get(), uv, views.range.Cpu(index));
        views.uvUavIndex = index++;
    }
    return views;
}

} // namespace

D3D12TextureViewSet CreateRgbaTextureViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav,
    DXGI_FORMAT viewFormat) {
    return CreateRgbaTextureViewSetImpl(
        context,
        D3D12ResourceView(texture),
        createSrv,
        createUav,
        viewFormat,
        "CreateRgbaTextureViewSet");
}

D3D12TextureViewSet CreateRgbaTextureViewSetFromView(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture,
    bool createSrv,
    bool createUav,
    DXGI_FORMAT viewFormat) {
    return CreateRgbaTextureViewSetImpl(
        context,
        texture,
        createSrv,
        createUav,
        viewFormat,
        "CreateRgbaTextureViewSetFromView");
}

D3D12TextureViewSet CreateNv12SrvViewSet(D3D12ProcessingContext& context, const D3D12Resource& texture) {
    if (texture.GetFormat() != DXGI_FORMAT_NV12) {
        throw UnsupportedFormatError("CreateNv12SrvViewSet: texture format must be NV12");
    }
    return CreateYuv420SrvUavViewSetImpl(
        context, D3D12ResourceView(texture), true, false, "CreateNv12SrvViewSet");
}

D3D12TextureViewSet CreateNv12UavViewSet(D3D12ProcessingContext& context, const D3D12Resource& texture) {
    if (texture.GetFormat() != DXGI_FORMAT_NV12) {
        throw UnsupportedFormatError("CreateNv12UavViewSet: texture format must be NV12");
    }
    return CreateYuv420SrvUavViewSetImpl(
        context, D3D12ResourceView(texture), false, true, "CreateNv12UavViewSet");
}

D3D12TextureViewSet CreateNv12SrvUavViewSet(D3D12ProcessingContext& context, const D3D12Resource& texture, bool createSrv, bool createUav) {
    if (texture.GetFormat() != DXGI_FORMAT_NV12) {
        throw UnsupportedFormatError("CreateNv12SrvUavViewSet: texture format must be NV12");
    }
    return CreateYuv420SrvUavViewSetImpl(
        context, D3D12ResourceView(texture), createSrv, createUav, "CreateNv12SrvUavViewSet");
}

D3D12TextureViewSet CreateYuv420SrvViewSet(D3D12ProcessingContext& context, const D3D12Resource& texture) {
    return CreateYuv420SrvUavViewSetImpl(
        context, D3D12ResourceView(texture), true, false, "CreateYuv420SrvViewSet");
}

D3D12TextureViewSet CreateYuv420UavViewSet(D3D12ProcessingContext& context, const D3D12Resource& texture) {
    return CreateYuv420SrvUavViewSetImpl(
        context, D3D12ResourceView(texture), false, true, "CreateYuv420UavViewSet");
}

D3D12TextureViewSet CreateYuv420SrvUavViewSet(
    D3D12ProcessingContext& context,
    const D3D12Resource& texture,
    bool createSrv,
    bool createUav) {
    return CreateYuv420SrvUavViewSetImpl(
        context,
        D3D12ResourceView(texture),
        createSrv,
        createUav,
        "CreateYuv420SrvUavViewSet");
}

D3D12TextureViewSet CreateYuv420SrvViewSetFromView(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture) {
    return CreateYuv420SrvUavViewSetImpl(
        context, texture, true, false, "CreateYuv420SrvViewSetFromView");
}

D3D12TextureViewSet CreateYuv420UavViewSetFromView(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture) {
    return CreateYuv420SrvUavViewSetImpl(
        context, texture, false, true, "CreateYuv420UavViewSetFromView");
}

D3D12TextureViewSet CreateYuv420SrvUavViewSetFromView(
    D3D12ProcessingContext& context,
    D3D12ResourceView texture,
    bool createSrv,
    bool createUav) {
    return CreateYuv420SrvUavViewSetImpl(
        context, texture, createSrv, createUav, "CreateYuv420SrvUavViewSetFromView");
}

} // namespace Processing
} // namespace D3D12CoreLib
