#include <D3D12Helper/D3D12Gpu/D3D12View.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace D3D12CoreLib {
namespace {

void RequireResource(const D3D12Resource& r, const char* fn) {
    if (!r.Get()) throw std::runtime_error(std::string(fn) + ": null resource");
}

void RequireTexture2D(const D3D12Resource& r, const char* fn) {
    RequireResource(r, fn);
    if (r.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error(std::string(fn) + ": resource is not Texture2D");
    }
}

void RequireBuffer(const D3D12Resource& r, const char* fn) {
    RequireResource(r, fn);
    if (r.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        throw std::runtime_error(std::string(fn) + ": resource is not Buffer");
    }
}

DXGI_FORMAT ResolveFormat(const D3D12Resource& r, DXGI_FORMAT f) {
    return (f == DXGI_FORMAT_UNKNOWN) ? r.GetDesc().Format : f;
}

UINT ResolveMipLevels(const D3D12_RESOURCE_DESC& desc, UINT mostDetailedMip, UINT mipLevels, const char* fn) {
    if (mostDetailedMip >= desc.MipLevels) throw std::runtime_error(std::string(fn) + ": mostDetailedMip out of range");
    const UINT available = desc.MipLevels - mostDetailedMip;
    return (mipLevels == UINT_MAX) ? available : std::min(mipLevels, available);
}

} // namespace

bool IsCpuDescriptorValid(D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept { return handle.ptr != 0; }
bool IsGpuDescriptorValid(D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept { return handle.ptr != 0; }
bool IsShaderVisibleDescriptor(const D3D12DescriptorHandle& handle) noexcept { return handle.shaderVisible && handle.gpu.ptr != 0; }

D3D12_SHADER_RESOURCE_VIEW_DESC MakeTexture2DSrvDesc(const D3D12Resource& texture, DXGI_FORMAT format, UINT mostDetailedMip, UINT mipLevels) {
    constexpr const char* fn = "MakeTexture2DSrvDesc";
    RequireTexture2D(texture, fn);
    const auto desc = texture.GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC out = {};
    out.Format = ResolveFormat(texture, format);
    out.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    out.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    out.Texture2D.MostDetailedMip = mostDetailedMip;
    out.Texture2D.MipLevels = ResolveMipLevels(desc, mostDetailedMip, mipLevels, fn);
    out.Texture2D.PlaneSlice = 0;
    out.Texture2D.ResourceMinLODClamp = 0.0f;
    return out;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeTexture2DUavDesc(const D3D12Resource& texture, DXGI_FORMAT format, UINT mipSlice) {
    constexpr const char* fn = "MakeTexture2DUavDesc";
    RequireTexture2D(texture, fn);
    if (mipSlice >= texture.GetDesc().MipLevels) throw std::runtime_error(std::string(fn) + ": mipSlice out of range");
    D3D12_UNORDERED_ACCESS_VIEW_DESC out = {};
    out.Format = ResolveFormat(texture, format);
    out.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    out.Texture2D.MipSlice = mipSlice;
    out.Texture2D.PlaneSlice = 0;
    return out;
}

D3D12_RENDER_TARGET_VIEW_DESC MakeTexture2DRtvDesc(const D3D12Resource& texture, DXGI_FORMAT format, UINT mipSlice) {
    constexpr const char* fn = "MakeTexture2DRtvDesc";
    RequireTexture2D(texture, fn);
    if (mipSlice >= texture.GetDesc().MipLevels) throw std::runtime_error(std::string(fn) + ": mipSlice out of range");
    D3D12_RENDER_TARGET_VIEW_DESC out = {};
    out.Format = ResolveFormat(texture, format);
    out.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    out.Texture2D.MipSlice = mipSlice;
    out.Texture2D.PlaneSlice = 0;
    return out;
}

D3D12_DEPTH_STENCIL_VIEW_DESC MakeTexture2DDsvDesc(const D3D12Resource& texture, DXGI_FORMAT format, UINT mipSlice, D3D12_DSV_FLAGS flags) {
    constexpr const char* fn = "MakeTexture2DDsvDesc";
    RequireTexture2D(texture, fn);
    if (mipSlice >= texture.GetDesc().MipLevels) throw std::runtime_error(std::string(fn) + ": mipSlice out of range");
    D3D12_DEPTH_STENCIL_VIEW_DESC out = {};
    out.Format = ResolveFormat(texture, format);
    out.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    out.Flags = flags;
    out.Texture2D.MipSlice = mipSlice;
    return out;
}

D3D12_SHADER_RESOURCE_VIEW_DESC MakeBufferSrvDesc(const D3D12Resource& buffer, UINT firstElement, UINT numElements, UINT structureByteStride, DXGI_FORMAT format) {
    constexpr const char* fn = "MakeBufferSrvDesc";
    RequireBuffer(buffer, fn);
    if (numElements == 0) throw std::runtime_error(std::string(fn) + ": numElements must be > 0");
    if (format == DXGI_FORMAT_UNKNOWN && structureByteStride == 0) throw std::runtime_error(std::string(fn) + ": structured buffer requires stride");
    D3D12_SHADER_RESOURCE_VIEW_DESC out = {};
    out.Format = format;
    out.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    out.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    out.Buffer.FirstElement = firstElement;
    out.Buffer.NumElements = numElements;
    out.Buffer.StructureByteStride = structureByteStride;
    out.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    return out;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeBufferUavDesc(const D3D12Resource& buffer, UINT firstElement, UINT numElements, UINT structureByteStride, DXGI_FORMAT format) {
    constexpr const char* fn = "MakeBufferUavDesc";
    RequireBuffer(buffer, fn);
    if (numElements == 0) throw std::runtime_error(std::string(fn) + ": numElements must be > 0");
    if (format == DXGI_FORMAT_UNKNOWN && structureByteStride == 0) throw std::runtime_error(std::string(fn) + ": structured buffer requires stride");
    D3D12_UNORDERED_ACCESS_VIEW_DESC out = {};
    out.Format = format;
    out.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    out.Buffer.FirstElement = firstElement;
    out.Buffer.NumElements = numElements;
    out.Buffer.StructureByteStride = structureByteStride;
    out.Buffer.CounterOffsetInBytes = 0;
    out.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    return out;
}

D3D12_CONSTANT_BUFFER_VIEW_DESC MakeConstantBufferViewDesc(const D3D12Resource& buffer, UINT64 byteOffset, UINT sizeBytes) {
    constexpr const char* fn = "MakeConstantBufferViewDesc";
    RequireBuffer(buffer, fn);
    const UINT64 width = buffer.GetDesc().Width;
    if (byteOffset > width) throw std::runtime_error(std::string(fn) + ": byteOffset out of range");
    const UINT resolvedSize = sizeBytes ? sizeBytes : static_cast<UINT>(width - byteOffset);
    if (byteOffset + resolvedSize > width) throw std::runtime_error(std::string(fn) + ": range out of bounds");
    D3D12_CONSTANT_BUFFER_VIEW_DESC out = {};
    out.BufferLocation = buffer.Get()->GetGPUVirtualAddress() + byteOffset;
    out.SizeInBytes = (resolvedSize + 255u) & ~255u;
    return out;
}

} // namespace D3D12CoreLib
