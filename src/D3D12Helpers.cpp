//
// D3D12Helpers.cpp
//
#include "D3D12Helpers.hpp"
#include "../D3D12Core/D3D12Barrier.hpp"
#include "../D3D12Core/ThrowIfFailed.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace D3D12CoreLib {

namespace {

D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type                 = type;
    hp.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask     = 1;
    hp.VisibleNodeMask      = 1;
    return hp;
}

UINT64 AlignUp(UINT64 value, UINT64 alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

UINT64 AlignConstantBufferSize(UINT64 sizeBytes) {
    if (sizeBytes == 0) {
        throw std::runtime_error("CreateConstantBuffer: sizeBytes must be > 0");
    }
    return AlignUp(sizeBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}

void CheckCpuDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, const char* name) {
    if (cpuHandle.ptr == 0) {
        throw std::runtime_error(std::string(name) + ": invalid CPU descriptor handle");
    }
}

void CheckBufferResource(const D3D12Resource& buffer, const char* name) {
    if (!buffer.Get()) {
        throw std::runtime_error(std::string(name) + ": null buffer");
    }
    if (buffer.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
        throw std::runtime_error(std::string(name) + ": resource is not a buffer");
    }
}

void CheckTexture2DResource(const D3D12Resource& texture, const char* name) {
    if (!texture.Get()) {
        throw std::runtime_error(std::string(name) + ": null texture");
    }
    if (texture.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error(std::string(name) + ": resource is not a Texture2D");
    }
}

UINT BytesPerElementForTypedBuffer(DXGI_FORMAT format, const char* name) {
    const UINT bytes = FormatUtil::BytesPerPixel(format);
    if (bytes == 0) {
        throw std::runtime_error(std::string(name) + ": unsupported typed buffer format");
    }
    return bytes;
}

UINT ResolveBufferViewStride(const D3D12Resource& buffer, UINT firstElement,
                             UINT numElements, UINT structureByteStride,
                             DXGI_FORMAT format, const char* name) {
    if (numElements == 0) {
        throw std::runtime_error(std::string(name) + ": numElements must be > 0");
    }

    const D3D12_RESOURCE_DESC desc = buffer.GetDesc();
    const UINT64 lastElement = static_cast<UINT64>(firstElement) + numElements;

    if (format != DXGI_FORMAT_UNKNOWN) {
        if (structureByteStride != 0) {
            throw std::runtime_error(std::string(name) + ": typed buffer view requires structureByteStride == 0");
        }
        const UINT bytes = BytesPerElementForTypedBuffer(format, name);
        if (lastElement > desc.Width / bytes) {
            throw std::runtime_error(std::string(name) + ": typed buffer view exceeds buffer size");
        }
        return 0;
    }

    if (structureByteStride == 0) {
        throw std::runtime_error(std::string(name) +
            ": structured buffer view requires explicit structureByteStride");
    }

    if (lastElement > desc.Width / structureByteStride) {
        throw std::runtime_error(std::string(name) + ": structured buffer view exceeds buffer size");
    }
    return structureByteStride;
}

UINT GetTextureSubresourceCount(const D3D12_RESOURCE_DESC& desc) {
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error("D3D12Helpers: only Texture2D upload is supported");
    }
    const UINT planeCount = FormatUtil::GetKnownPlaneCount(desc.Format);
    if (planeCount == 0) {
        throw std::runtime_error("D3D12Helpers: unknown texture format for subresource count");
    }
    return static_cast<UINT>(desc.MipLevels) * static_cast<UINT>(desc.DepthOrArraySize) * planeCount;
}

void ValidateSubresourceRange(const D3D12_RESOURCE_DESC& desc,
                              UINT firstSubresource,
                              UINT subresourceCount) {
    if (subresourceCount == 0) {
        throw std::runtime_error("D3D12Helpers: subresourceCount must be > 0");
    }
    const UINT totalSubresources = GetTextureSubresourceCount(desc);
    if (firstSubresource >= totalSubresources ||
        subresourceCount > totalSubresources - firstSubresource) {
        throw std::runtime_error("D3D12Helpers: subresource range is out of bounds");
    }
}

void ValidateSingleSubresourceTexture(const D3D12_RESOURCE_DESC& desc, const char* name) {
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error(std::string(name) + ": only Texture2D is supported");
    }
    if (desc.MipLevels != 1 || desc.DepthOrArraySize != 1 ||
        FormatUtil::GetKnownPlaneCount(desc.Format) != 1) {
        throw std::runtime_error(std::string(name) +
            ": texture has multiple subresources; use RecordUploadTextureSubresources");
    }
}

UINT BytesPerPixelChecked(DXGI_FORMAT format) {
    if (FormatUtil::IsPlanarFormat(format)) {
        throw std::runtime_error(
            "D3D12Helpers: planar formats require RecordUploadTextureSubresources");
    }
    const UINT bpp = FormatUtil::BytesPerPixel(format);
    if (bpp == 0) {
        throw std::runtime_error(
            "D3D12Helpers: unsupported / block-compressed format for from-memory upload");
    }
    return bpp;
}

// 1サブリソース分の footprint を取得する。
struct SingleFootprint {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT   numRows    = 0;
    UINT64 rowSize    = 0;
    UINT64 totalBytes = 0;
};

SingleFootprint QueryFootprint(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc) {
    SingleFootprint fp;
    device->GetCopyableFootprints(
        &desc, 0, 1, 0, &fp.layout, &fp.numRows, &fp.rowSize, &fp.totalBytes);
    return fp;
}

struct FootprintSet {
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
    std::vector<UINT> numRows;
    std::vector<UINT64> rowSizes;
    UINT64 totalBytes = 0;
};

FootprintSet QueryFootprints(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc,
                             UINT firstSubresource, UINT subresourceCount,
                             UINT64 baseOffset = 0) {
    FootprintSet fp;
    fp.layouts.resize(subresourceCount);
    fp.numRows.resize(subresourceCount);
    fp.rowSizes.resize(subresourceCount);
    device->GetCopyableFootprints(
        &desc,
        firstSubresource,
        subresourceCount,
        baseOffset,
        fp.layouts.data(),
        fp.numRows.data(),
        fp.rowSizes.data(),
        &fp.totalBytes);
    return fp;
}

// upload(mapped) へ src を行ピッチを合わせて書き込む。
void CopyRowsToUpload(uint8_t* mapped, const SingleFootprint& fp,
                      const void* src, UINT effectiveSrcPitch) {
    const auto* s = static_cast<const uint8_t*>(src);
    uint8_t* base = mapped + fp.layout.Offset;
    const UINT dstPitch = fp.layout.Footprint.RowPitch;
    for (UINT y = 0; y < fp.numRows; ++y) {
        std::memcpy(base + static_cast<size_t>(y) * dstPitch,
                    s + static_cast<size_t>(y) * effectiveSrcPitch,
                    static_cast<size_t>(fp.rowSize));
    }
}

void CopySubresourceToUpload(uint8_t* mappedBase,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout,
                             UINT numRows,
                             UINT64 rowSize,
                             const D3D12TextureSubresourceData& src,
                             UINT subresourceIndexForError) {
    if (!src.data) {
        throw std::runtime_error("RecordUploadTextureSubresources: null subresource data");
    }

    const UINT64 srcRowPitch = (src.rowPitch != 0) ? src.rowPitch : rowSize;
    if (srcRowPitch < rowSize) {
        throw std::runtime_error("RecordUploadTextureSubresources: src rowPitch is smaller than rowSize");
    }

    const UINT depth = layout.Footprint.Depth;
    const UINT64 srcSlicePitch = (src.slicePitch != 0)
        ? src.slicePitch
        : srcRowPitch * numRows;
    if (srcSlicePitch < srcRowPitch * numRows) {
        throw std::runtime_error("RecordUploadTextureSubresources: src slicePitch is too small");
    }

    const auto* srcBytes = static_cast<const uint8_t*>(src.data);
    uint8_t* dstBytes = mappedBase + layout.Offset;
    const UINT64 dstRowPitch = layout.Footprint.RowPitch;
    const UINT64 dstSlicePitch = dstRowPitch * numRows;

    for (UINT z = 0; z < depth; ++z) {
        for (UINT y = 0; y < numRows; ++y) {
            std::memcpy(
                dstBytes + static_cast<size_t>(z * dstSlicePitch + y * dstRowPitch),
                srcBytes + static_cast<size_t>(z * srcSlicePitch + y * srcRowPitch),
                static_cast<size_t>(rowSize));
        }
    }
    (void)subresourceIndexForError;
}

void RecordCopyAndTransition(D3D12CommandContext& ctx,
                             D3D12Resource& dstTexture,
                             ID3D12Resource* uploadResource,
                             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout,
                             D3D12_RESOURCE_STATES finalState) {
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource        = dstTexture.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource       = uploadResource;
    srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = layout;

    ctx.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    if (finalState != D3D12_RESOURCE_STATE_COPY_DEST) {
        ctx.ResourceBarrier(
            MakeTransitionBarrier(dstTexture.Get(),
                                  D3D12_RESOURCE_STATE_COPY_DEST, finalState));
    }
}

void RecordCopySubresourcesAndTransition(D3D12CommandContext& ctx,
                                         D3D12Resource& dstTexture,
                                         ID3D12Resource* uploadResource,
                                         const FootprintSet& fp,
                                         UINT firstSubresource,
                                         D3D12_RESOURCE_STATES finalState) {
    auto* cmd = ctx.GetCommandList();
    for (UINT i = 0; i < static_cast<UINT>(fp.layouts.size()); ++i) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource        = dstTexture.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = firstSubresource + i;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource       = uploadResource;
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fp.layouts[i];

        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

    if (finalState != D3D12_RESOURCE_STATE_COPY_DEST) {
        ctx.ResourceBarrier(
            MakeTransitionBarrier(dstTexture.Get(),
                                  D3D12_RESOURCE_STATE_COPY_DEST, finalState));
    }
}

void CopySubresourcesToMappedUpload(uint8_t* mappedBase,
                                    const FootprintSet& fp,
                                    const D3D12TextureSubresourceData* subresources) {
    for (UINT i = 0; i < static_cast<UINT>(fp.layouts.size()); ++i) {
        CopySubresourceToUpload(mappedBase, fp.layouts[i], fp.numRows[i], fp.rowSizes[i],
                                subresources[i], i);
    }
}

} // unnamed namespace

// ==========================================================================

D3D12Resource CreateBuffer(D3D12Core& core, UINT64 sizeBytes, D3D12_HEAP_TYPE heapType,
                           D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags,
                           D3D12_HEAP_FLAGS heapFlags) {
    if (sizeBytes == 0) {
        throw std::runtime_error("CreateBuffer: sizeBytes must be > 0");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = sizeBytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = flags;

    const D3D12_HEAP_PROPERTIES hp = MakeHeapProps(heapType);

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &hp, heapFlags, &desc, initialState, nullptr, IID_PPV_ARGS(&resource)));
    return D3D12Resource(std::move(resource), initialState);
}

D3D12Resource CreateTexture2D(D3D12Core& core, UINT width, UINT height, DXGI_FORMAT format,
                              D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags,
                              UINT16 arraySize, UINT16 mipLevels,
                              D3D12_HEAP_FLAGS heapFlags) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("CreateTexture2D: width and height must be > 0");
    }
    if (arraySize == 0 || mipLevels == 0) {
        throw std::runtime_error("CreateTexture2D: arraySize and mipLevels must be > 0");
    }
    if (FormatUtil::RequiresEvenSize(format) && ((width % 2) != 0 || (height % 2) != 0)) {
        throw std::runtime_error("CreateTexture2D: format requires even width and height");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = width;
    desc.Height           = height;
    desc.DepthOrArraySize = arraySize;
    desc.MipLevels        = mipLevels;
    desc.Format           = format;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags            = flags;

    const D3D12_HEAP_PROPERTIES hp = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);

    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &hp, heapFlags, &desc, initialState, nullptr, IID_PPV_ARGS(&resource)));
    return D3D12Resource(std::move(resource), initialState);
}

D3D12Resource CreateStructuredBuffer(D3D12Core& core, UINT elementCount, UINT elementStride,
                                     D3D12_HEAP_TYPE heapType,
                                     D3D12_RESOURCE_STATES initialState,
                                     D3D12_RESOURCE_FLAGS flags,
                                     D3D12_HEAP_FLAGS heapFlags) {
    if (elementCount == 0) {
        throw std::runtime_error("CreateStructuredBuffer: elementCount must be > 0");
    }
    if (elementStride == 0) {
        throw std::runtime_error("CreateStructuredBuffer: elementStride must be > 0");
    }
    const UINT64 sizeBytes = static_cast<UINT64>(elementCount) * elementStride;
    return CreateBuffer(core, sizeBytes, heapType, initialState, flags, heapFlags);
}

D3D12Resource CreateConstantBuffer(D3D12Core& core, UINT64 sizeBytes,
                                   D3D12_HEAP_TYPE heapType,
                                   D3D12_RESOURCE_STATES initialState,
                                   D3D12_HEAP_FLAGS heapFlags) {
    return CreateBuffer(core,
                        AlignConstantBufferSize(sizeBytes),
                        heapType,
                        initialState,
                        D3D12_RESOURCE_FLAG_NONE,
                        heapFlags);
}

D3D12Resource CreateSharedTexture2D(D3D12Core& core, UINT width, UINT height,
                                    DXGI_FORMAT format,
                                    D3D12_RESOURCE_STATES initialState,
                                    D3D12_RESOURCE_FLAGS flags,
                                    UINT16 arraySize, UINT16 mipLevels) {
    return CreateTexture2D(core,
                           width,
                           height,
                           format,
                           initialState,
                           flags,
                           arraySize,
                           mipLevels,
                           D3D12_HEAP_FLAG_SHARED);
}

UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture) {
    CheckTexture2DResource(dstTexture, "GetRequiredUploadSize");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSingleSubresourceTexture(desc, "GetRequiredUploadSize");
    return QueryFootprint(core.GetDevice(), desc).totalBytes;
}

UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture,
                             UINT firstSubresource, UINT subresourceCount) {
    CheckTexture2DResource(dstTexture, "GetRequiredUploadSize");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSubresourceRange(desc, firstSubresource, subresourceCount);
    return QueryFootprints(core.GetDevice(), desc, firstSubresource, subresourceCount).totalBytes;
}

D3D12Resource CreateTexture2DFromMemory(D3D12Core& core, const void* data,
                                        UINT width, UINT height, DXGI_FORMAT format,
                                        UINT srcRowPitch, D3D12_RESOURCE_STATES finalState) {
    if (!data) throw std::runtime_error("CreateTexture2DFromMemory: null data");

    const UINT bpp = BytesPerPixelChecked(format);
    const UINT effectiveSrcPitch = (srcRowPitch != 0) ? srcRowPitch : (width * bpp);

    // COPY_DEST で作成
    D3D12Resource tex = CreateTexture2D(core, width, height, format,
                                        D3D12_RESOURCE_STATE_COPY_DEST);

    const D3D12_RESOURCE_DESC desc = tex.GetDesc();
    ValidateSingleSubresourceTexture(desc, "CreateTexture2DFromMemory");
    const SingleFootprint fp = QueryFootprint(core.GetDevice(), desc);

    // Upload Buffer 確保（このスコープ内のローカル: 末尾の WaitIdle 後に安全に破棄）
    D3D12UploadBuffer upload;
    upload.Initialize(core.GetDevice(), fp.totalBytes);
    CopyRowsToUpload(static_cast<uint8_t*>(upload.Map()), fp, data, effectiveSrcPitch);

    // Copy を Direct Queue で実行（COPY_DEST→任意状態の遷移が必要なため Direct を使う）
    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();
    RecordCopyAndTransition(ctx, tex, upload.Get(), fp.layout, finalState);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitIdle(); // upload を安全に破棄するため完了を待つ

    tex.SetState(finalState);
    return tex;
}

D3D12Resource CreateTexture2DFromRGBA(D3D12Core& core, const uint8_t* rgba,
                                      UINT width, UINT height,
                                      D3D12_RESOURCE_STATES finalState) {
    return CreateTexture2DFromMemory(core, rgba, width, height,
                                     DXGI_FORMAT_R8G8B8A8_UNORM, width * 4, finalState);
}

std::vector<uint8_t> ExpandRGBtoRGBA(const uint8_t* rgb, UINT width, UINT height, uint8_t alpha) {
    if (!rgb) throw std::runtime_error("ExpandRGBtoRGBA: null input");
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> out(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i) {
        out[i * 4 + 0] = rgb[i * 3 + 0];
        out[i * 4 + 1] = rgb[i * 3 + 1];
        out[i * 4 + 2] = rgb[i * 3 + 2];
        out[i * 4 + 3] = alpha;
    }
    return out;
}

D3D12Resource CreateTexture2DFromRGB(D3D12Core& core, const uint8_t* rgb,
                                     UINT width, UINT height, uint8_t alpha,
                                     D3D12_RESOURCE_STATES finalState) {
    const std::vector<uint8_t> rgba = ExpandRGBtoRGBA(rgb, width, height, alpha);
    return CreateTexture2DFromMemory(core, rgba.data(), width, height,
                                     DXGI_FORMAT_R8G8B8A8_UNORM, width * 4, finalState);
}

void RecordUploadTexture2D(D3D12Core& core, D3D12CommandContext& ctx,
                           D3D12Resource& dstTexture, D3D12UploadBuffer& upload,
                           const void* data, UINT width, UINT height, DXGI_FORMAT format,
                           UINT srcRowPitch, D3D12_RESOURCE_STATES finalState) {
    if (!data) throw std::runtime_error("RecordUploadTexture2D: null data");

    const UINT bpp = BytesPerPixelChecked(format);
    const UINT effectiveSrcPitch = (srcRowPitch != 0) ? srcRowPitch : (width * bpp);

    CheckTexture2DResource(dstTexture, "RecordUploadTexture2D");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSingleSubresourceTexture(desc, "RecordUploadTexture2D");
    const SingleFootprint fp = QueryFootprint(core.GetDevice(), desc);

    if (upload.GetSizeBytes() < fp.totalBytes) {
        throw std::runtime_error("RecordUploadTexture2D: upload buffer too small "
                                 "(use GetRequiredUploadSize to size it)");
    }

    CopyRowsToUpload(static_cast<uint8_t*>(upload.Map()), fp, data, effectiveSrcPitch);
    RecordCopyAndTransition(ctx, dstTexture, upload.Get(), fp.layout, finalState);
    dstTexture.SetState(finalState);
}

void RecordUploadTexture2D(D3D12Core& core, D3D12CommandContext& ctx,
                           D3D12Resource& dstTexture, D3D12UploadRing& ring,
                           const void* data, UINT width, UINT height, DXGI_FORMAT format,
                           UINT srcRowPitch, D3D12_RESOURCE_STATES finalState) {
    if (!data) throw std::runtime_error("RecordUploadTexture2D(Ring): null data");

    const UINT bpp = BytesPerPixelChecked(format);
    const UINT effectiveSrcPitch = (srcRowPitch != 0) ? srcRowPitch : (width * bpp);

    CheckTexture2DResource(dstTexture, "RecordUploadTexture2D(Ring)");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSingleSubresourceTexture(desc, "RecordUploadTexture2D(Ring)");
    const SingleFootprint fp = QueryFootprint(core.GetDevice(), desc);

    D3D12UploadRing::Allocation alloc =
        ring.Allocate(fp.totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    CopyRowsToUpload(static_cast<uint8_t*>(alloc.cpuPtr), fp, data, effectiveSrcPitch);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT ringLayout = fp.layout;
    ringLayout.Offset = alloc.offset;

    RecordCopyAndTransition(ctx, dstTexture, alloc.resource, ringLayout, finalState);
    dstTexture.SetState(finalState);
}

void RecordUploadTextureSubresources(D3D12Core& core, D3D12CommandContext& ctx,
                                     D3D12Resource& dstTexture,
                                     D3D12UploadBuffer& upload,
                                     const D3D12TextureSubresourceData* subresources,
                                     UINT firstSubresource,
                                     UINT subresourceCount,
                                     D3D12_RESOURCE_STATES finalState) {
    if (!subresources) {
        throw std::runtime_error("RecordUploadTextureSubresources: null subresources");
    }
    CheckTexture2DResource(dstTexture, "RecordUploadTextureSubresources");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSubresourceRange(desc, firstSubresource, subresourceCount);

    FootprintSet fp = QueryFootprints(core.GetDevice(), desc, firstSubresource, subresourceCount);
    if (upload.GetSizeBytes() < fp.totalBytes) {
        throw std::runtime_error("RecordUploadTextureSubresources: upload buffer too small");
    }

    CopySubresourcesToMappedUpload(static_cast<uint8_t*>(upload.Map()), fp, subresources);
    RecordCopySubresourcesAndTransition(ctx, dstTexture, upload.Get(), fp, firstSubresource, finalState);
    dstTexture.SetState(finalState);
}

void RecordUploadTextureSubresources(D3D12Core& core, D3D12CommandContext& ctx,
                                     D3D12Resource& dstTexture,
                                     D3D12UploadRing& ring,
                                     const D3D12TextureSubresourceData* subresources,
                                     UINT firstSubresource,
                                     UINT subresourceCount,
                                     D3D12_RESOURCE_STATES finalState) {
    if (!subresources) {
        throw std::runtime_error("RecordUploadTextureSubresources(Ring): null subresources");
    }
    CheckTexture2DResource(dstTexture, "RecordUploadTextureSubresources(Ring)");
    const D3D12_RESOURCE_DESC desc = dstTexture.GetDesc();
    ValidateSubresourceRange(desc, firstSubresource, subresourceCount);

    FootprintSet fp = QueryFootprints(core.GetDevice(), desc, firstSubresource, subresourceCount);
    D3D12UploadRing::Allocation alloc =
        ring.Allocate(fp.totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    for (auto& layout : fp.layouts) {
        layout.Offset += alloc.offset;
    }

    uint8_t* ringBase = static_cast<uint8_t*>(alloc.cpuPtr) - static_cast<size_t>(alloc.offset);
    CopySubresourcesToMappedUpload(ringBase, fp, subresources);
    RecordCopySubresourcesAndTransition(ctx, dstTexture, alloc.resource, fp, firstSubresource, finalState);
    dstTexture.SetState(finalState);
}

// ===========================================================================
// SRV / UAV / RTV / DSV ヘルパ
// ==========================================================================
void CreateSrv(D3D12Core& core, ID3D12Resource* resource,
               const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
               D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    if (!resource) throw std::runtime_error("CreateSrv: null resource");
    CheckCpuDescriptor(cpuHandle, "CreateSrv");
    core.GetDevice()->CreateShaderResourceView(resource, &desc, cpuHandle);
}

void CreateUav(D3D12Core& core, ID3D12Resource* resource,
               const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
               D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
               ID3D12Resource* counterResource) {
    if (!resource) throw std::runtime_error("CreateUav: null resource");
    CheckCpuDescriptor(cpuHandle, "CreateUav");

    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    if (!(resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
        throw std::runtime_error("CreateUav: resource was not created with "
                                 "D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS");
    }

    core.GetDevice()->CreateUnorderedAccessView(resource, counterResource, &desc, cpuHandle);
}

void CreateRtv(D3D12Core& core, ID3D12Resource* resource,
               const D3D12_RENDER_TARGET_VIEW_DESC& desc,
               D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    if (!resource) throw std::runtime_error("CreateRtv: null resource");
    CheckCpuDescriptor(cpuHandle, "CreateRtv");

    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    if (!(resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) {
        throw std::runtime_error("CreateRtv: resource was not created with "
                                 "D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET");
    }

    core.GetDevice()->CreateRenderTargetView(resource, &desc, cpuHandle);
}

void CreateDsv(D3D12Core& core, ID3D12Resource* resource,
               const D3D12_DEPTH_STENCIL_VIEW_DESC& desc,
               D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    if (!resource) throw std::runtime_error("CreateDsv: null resource");
    CheckCpuDescriptor(cpuHandle, "CreateDsv");

    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    if (!(resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
        throw std::runtime_error("CreateDsv: resource was not created with "
                                 "D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL");
    }

    core.GetDevice()->CreateDepthStencilView(resource, &desc, cpuHandle);
}

void CreateTexture2DSrv(D3D12Core& core, const D3D12Resource& texture,
                        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, DXGI_FORMAT format) {
    CheckTexture2DResource(texture, "CreateTexture2DSrv");

    const D3D12_RESOURCE_DESC desc = texture.GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = (format == DXGI_FORMAT_UNKNOWN) ? desc.Format : format;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = desc.MipLevels;

    CreateSrv(core, texture.Get(), srvDesc, cpuHandle);
}

void CreateTexture2DUav(D3D12Core& core, const D3D12Resource& texture,
                        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, DXGI_FORMAT format,
                        UINT mipSlice) {
    CheckTexture2DResource(texture, "CreateTexture2DUav");

    const D3D12_RESOURCE_DESC desc = texture.GetDesc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format             = (format == DXGI_FORMAT_UNKNOWN) ? desc.Format : format;
    uavDesc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = mipSlice;

    CreateUav(core, texture.Get(), uavDesc, cpuHandle);
}

void CreateBufferSrv(D3D12Core& core, const D3D12Resource& buffer,
                     D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                     UINT firstElement, UINT numElements,
                     UINT structureByteStride, DXGI_FORMAT format) {
    CheckBufferResource(buffer, "CreateBufferSrv");
    const UINT stride = ResolveBufferViewStride(buffer, firstElement, numElements,
                                               structureByteStride, format,
                                               "CreateBufferSrv");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = firstElement;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Buffer.StructureByteStride = stride;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    CreateSrv(core, buffer.Get(), srvDesc, cpuHandle);
}

void CreateBufferUav(D3D12Core& core, const D3D12Resource& buffer,
                     D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                     UINT firstElement, UINT numElements,
                     UINT structureByteStride, DXGI_FORMAT format,
                     ID3D12Resource* counterResource) {
    CheckBufferResource(buffer, "CreateBufferUav");
    const UINT stride = ResolveBufferViewStride(buffer, firstElement, numElements,
                                               structureByteStride, format,
                                               "CreateBufferUav");

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = firstElement;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.StructureByteStride = stride;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    CreateUav(core, buffer.Get(), uavDesc, cpuHandle, counterResource);
}

void CreateConstantBufferView(D3D12Core& core, const D3D12Resource& buffer,
                              D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                              UINT64 byteOffset, UINT sizeBytes) {
    CheckBufferResource(buffer, "CreateConstantBufferView");
    CheckCpuDescriptor(cpuHandle, "CreateConstantBufferView");

    if ((byteOffset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) != 0) {
        throw std::runtime_error("CreateConstantBufferView: byteOffset must be 256-byte aligned");
    }

    const D3D12_RESOURCE_DESC desc = buffer.GetDesc();
    if (byteOffset >= desc.Width) {
        throw std::runtime_error("CreateConstantBufferView: byteOffset is out of range");
    }

    UINT64 resolvedSize = (sizeBytes != 0) ? sizeBytes : (desc.Width - byteOffset);
    if ((resolvedSize % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) != 0) {
        throw std::runtime_error("CreateConstantBufferView: sizeBytes must be 256-byte aligned");
    }
    if (byteOffset + resolvedSize > desc.Width) {
        throw std::runtime_error("CreateConstantBufferView: view exceeds buffer size");
    }
    if (resolvedSize > 0xffffffffull) {
        throw std::runtime_error("CreateConstantBufferView: sizeBytes is too large");
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = buffer.Get()->GetGPUVirtualAddress() + byteOffset;
    cbvDesc.SizeInBytes = static_cast<UINT>(resolvedSize);
    core.GetDevice()->CreateConstantBufferView(&cbvDesc, cpuHandle);
}

void CreateTexture2DRtv(D3D12Core& core, const D3D12Resource& texture,
                        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                        DXGI_FORMAT format, UINT mipSlice) {
    CheckTexture2DResource(texture, "CreateTexture2DRtv");
    const D3D12_RESOURCE_DESC desc = texture.GetDesc();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = (format == DXGI_FORMAT_UNKNOWN) ? desc.Format : format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = mipSlice;
    rtvDesc.Texture2D.PlaneSlice = 0;
    CreateRtv(core, texture.Get(), rtvDesc, cpuHandle);
}

void CreateTexture2DDsv(D3D12Core& core, const D3D12Resource& texture,
                        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                        DXGI_FORMAT format, UINT mipSlice) {
    CheckTexture2DResource(texture, "CreateTexture2DDsv");
    const D3D12_RESOURCE_DESC desc = texture.GetDesc();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = (format == DXGI_FORMAT_UNKNOWN) ? desc.Format : format;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D.MipSlice = mipSlice;
    CreateDsv(core, texture.Get(), dsvDesc, cpuHandle);
}

// ===========================================================================
// Sampler ヘルパ
// ==========================================================================
D3D12_SAMPLER_DESC MakeLinearClampSamplerDesc() {
    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MipLODBias = 0.0f;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.BorderColor[0] = 0.0f;
    desc.BorderColor[1] = 0.0f;
    desc.BorderColor[2] = 0.0f;
    desc.BorderColor[3] = 0.0f;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    return desc;
}

D3D12_SAMPLER_DESC MakePointClampSamplerDesc() {
    D3D12_SAMPLER_DESC desc = MakeLinearClampSamplerDesc();
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    return desc;
}

void CreateSampler(D3D12Core& core, const D3D12_SAMPLER_DESC& desc,
                   D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) {
    CheckCpuDescriptor(cpuHandle, "CreateSampler");
    core.GetDevice()->CreateSampler(&desc, cpuHandle);
}

} // namespace D3D12CoreLib
