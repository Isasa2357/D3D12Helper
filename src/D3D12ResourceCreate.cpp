//
// D3D12ResourceCreate.cpp
//
#include <D3D12Helper/D3D12Gpu/D3D12ResourceCreate.hpp>
#include <D3D12Helper/D3D12Core/D3D12FormatUtil.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {
namespace {

void ValidateNodeMasks(UINT creationNodeMask, UINT visibleNodeMask, const char* functionName) {
    if (creationNodeMask == 0) {
        throw std::runtime_error(std::string(functionName) + ": creationNodeMask must not be zero");
    }
    if (visibleNodeMask == 0) {
        throw std::runtime_error(std::string(functionName) + ": visibleNodeMask must not be zero");
    }
    if ((visibleNodeMask & creationNodeMask) != creationNodeMask) {
        throw std::runtime_error(std::string(functionName) +
            ": visibleNodeMask must include creationNodeMask");
    }
}

void ValidateHeapInitialState(
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    const char* functionName) {

    if (heapType == D3D12_HEAP_TYPE_UPLOAD &&
        initialState != D3D12_RESOURCE_STATE_GENERIC_READ) {
        throw std::runtime_error(std::string(functionName) +
            ": UPLOAD heap requires GENERIC_READ initial state");
    }
    if (heapType == D3D12_HEAP_TYPE_READBACK &&
        initialState != D3D12_RESOURCE_STATE_COPY_DEST) {
        throw std::runtime_error(std::string(functionName) +
            ": READBACK heap requires COPY_DEST initial state");
    }
}

D3D12_HEAP_PROPERTIES MakeHeapProperties(
    D3D12_HEAP_TYPE heapType,
    D3D12_CPU_PAGE_PROPERTY cpuPageProperty,
    D3D12_MEMORY_POOL memoryPoolPreference,
    UINT creationNodeMask,
    UINT visibleNodeMask) noexcept {

    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = heapType;
    properties.CPUPageProperty = cpuPageProperty;
    properties.MemoryPoolPreference = memoryPoolPreference;
    properties.CreationNodeMask = creationNodeMask;
    properties.VisibleNodeMask = visibleNodeMask;
    return properties;
}

} // namespace

D3D12Resource CreateBufferDetailed(
    D3D12Core& core,
    const D3D12BufferCreateDesc& desc) {

    if (desc.sizeBytes == 0) {
        throw std::runtime_error("CreateBufferDetailed: sizeBytes must be > 0");
    }
    ValidateNodeMasks(desc.creationNodeMask, desc.visibleNodeMask, "CreateBufferDetailed");
    ValidateHeapInitialState(desc.heapType, desc.initialState, "CreateBufferDetailed");

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = desc.alignment;
    resourceDesc.Width = desc.sizeBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = desc.resourceFlags;

    const D3D12_HEAP_PROPERTIES heapProperties = MakeHeapProperties(
        desc.heapType,
        desc.cpuPageProperty,
        desc.memoryPoolPreference,
        desc.creationNodeMask,
        desc.visibleNodeMask);

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &heapProperties,
        desc.heapFlags,
        &resourceDesc,
        desc.initialState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    return D3D12Resource(std::move(resource), desc.initialState);
}

D3D12Resource CreateTexture2DDetailed(
    D3D12Core& core,
    const D3D12Texture2DCreateDesc& desc) {

    if (desc.width == 0 || desc.height == 0) {
        throw std::runtime_error("CreateTexture2DDetailed: width and height must be > 0");
    }
    if (desc.arraySize == 0 || desc.mipLevels == 0) {
        throw std::runtime_error("CreateTexture2DDetailed: arraySize and mipLevels must be > 0");
    }
    if (desc.sampleDesc.Count == 0) {
        throw std::runtime_error("CreateTexture2DDetailed: sample count must be > 0");
    }
    if (FormatUtil::RequiresEvenSize(desc.format) &&
        (((desc.width % 2u) != 0u) || ((desc.height % 2u) != 0u))) {
        throw std::runtime_error(
            "CreateTexture2DDetailed: format requires even width and height");
    }

    ValidateNodeMasks(
        desc.creationNodeMask,
        desc.visibleNodeMask,
        "CreateTexture2DDetailed");
    ValidateHeapInitialState(
        desc.heapType,
        desc.initialState,
        "CreateTexture2DDetailed");

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = desc.alignment;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.arraySize;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = desc.format;
    resourceDesc.SampleDesc = desc.sampleDesc;
    resourceDesc.Layout = desc.layout;
    resourceDesc.Flags = desc.resourceFlags;

    const D3D12_HEAP_PROPERTIES heapProperties = MakeHeapProperties(
        desc.heapType,
        desc.cpuPageProperty,
        desc.memoryPoolPreference,
        desc.creationNodeMask,
        desc.visibleNodeMask);

    const D3D12_CLEAR_VALUE* clearValue = desc.clearValue
        ? &desc.clearValue.value()
        : nullptr;

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &heapProperties,
        desc.heapFlags,
        &resourceDesc,
        desc.initialState,
        clearValue,
        IID_PPV_ARGS(&resource)));

    return D3D12Resource(std::move(resource), desc.initialState);
}

} // namespace D3D12CoreLib
