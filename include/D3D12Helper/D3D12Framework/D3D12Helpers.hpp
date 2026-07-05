#pragma once
//
// D3D12Helpers.hpp
// すべて第一引数に D3D12Core& を取る自由関数群。
// 上位サブシステム（Window / D3D12Camera 等）は「core を1つ渡して1行呼ぶ」だけで
// D3D12 リソースを得られる。リソースの所有権は呼び出し側（戻り値）に渡る。
//
#include "../D3D12Core/D3D12Core.hpp"
#include "../D3D12Core/D3D12FormatUtil.hpp"
#include "D3D12Resource.hpp"
#include "D3D12UploadBuffer.hpp"
#include "D3D12UploadRing.hpp"

#include <cstdint>
#include <vector>

namespace D3D12CoreLib {

// --------------------------------------------------------------------------
// 生成（待ち無し・空リソース）
// --------------------------------------------------------------------------
D3D12Resource CreateBuffer(
    D3D12Core& core,
    UINT64 sizeBytes,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateTexture2D(
    D3D12Core& core,
    UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    UINT16 arraySize = 1, UINT16 mipLevels = 1,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateStructuredBuffer(
    D3D12Core& core,
    UINT elementCount,
    UINT elementStride,
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateConstantBuffer(
    D3D12Core& core,
    UINT64 sizeBytes,
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_GENERIC_READ,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateSharedTexture2D(
    D3D12Core& core,
    UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
    UINT16 arraySize = 1, UINT16 mipLevels = 1);

// --------------------------------------------------------------------------
// CPU 配列 → Texture（同期）
//   単一 subresource の 2D Texture 用。
//   planar / mipmapped / array texture など複数 subresource を持つ texture には
//   RecordUploadTextureSubresources を使うこと。
// --------------------------------------------------------------------------
D3D12Resource CreateTexture2DFromMemory(
    D3D12Core& core,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0, // 0 なら width * BytesPerPixel(format)
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// RGBA8（4byte/px）配列から作る（同期）。
D3D12Resource CreateTexture2DFromRGBA(
    D3D12Core& core,
    const uint8_t* rgba, UINT width, UINT height,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// RGB（3byte/px）配列を内部で RGBA8 へ展開して作る（同期）。
//   DXGI に 24bit RGB フォーマットが無いため、alpha を補って R8G8B8A8_UNORM にする。
D3D12Resource CreateTexture2DFromRGB(
    D3D12Core& core,
    const uint8_t* rgb, UINT width, UINT height,
    uint8_t alpha = 255,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// RGB → RGBA8 展開ユーティリティ（新規バッファを返す）。
std::vector<uint8_t> ExpandRGBtoRGBA(
    const uint8_t* rgb, UINT width, UINT height, uint8_t alpha = 255);

// --------------------------------------------------------------------------
// ホットパス用: コマンドを積むだけ（Wait しない）。
//   単一 subresource の 2D Texture 用。
//   planar / mipmapped / array texture など複数 subresource を持つ texture には
//   RecordUploadTextureSubresources を使うこと。
// --------------------------------------------------------------------------
void RecordUploadTexture2D(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadBuffer& upload,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void RecordUploadTexture2D(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadRing& ring,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

struct D3D12TextureSubresourceData {
    const void* data = nullptr;
    UINT64 rowPitch = 0;   // 0 なら GetCopyableFootprints の rowSize を使う
    UINT64 slicePitch = 0; // 0 なら rowPitch * numRows を使う
};

// dstTexture を CPU からアップロードするのに必要な Upload Buffer の最小サイズを返す。
// 既存 overload は subresource 0 のみを対象にする。
UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture);
UINT64 GetRequiredUploadSize(
    D3D12Core& core,
    const D3D12Resource& dstTexture,
    UINT firstSubresource,
    UINT subresourceCount);

// 複数 subresource をまとめて upload する汎用 API。
// dstTexture は COPY_DEST 状態であること。コピー後、finalState へ全 subresource を遷移する。
void RecordUploadTextureSubresources(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadBuffer& upload,
    const D3D12TextureSubresourceData* subresources,
    UINT firstSubresource,
    UINT subresourceCount,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void RecordUploadTextureSubresources(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadRing& ring,
    const D3D12TextureSubresourceData* subresources,
    UINT firstSubresource,
    UINT subresourceCount,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// --------------------------------------------------------------------------
// ディスクリプタ作成ヘルパ（指定の CPU ハンドルに View を作る）
// --------------------------------------------------------------------------

// Full-desc 版。特殊 view / plane view / array view などを呼び出し側が明示的に指定する。
void CreateSrv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

void CreateUav(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    ID3D12Resource* counterResource = nullptr);

void CreateRtv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

void CreateDsv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

// Texture2D 用 SRV を作る。format に DXGI_FORMAT_UNKNOWN を渡すと texture の format を使う。
void CreateTexture2DSrv(
    D3D12Core& core,
    const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

// Texture2D 用 UAV を作る。
void CreateTexture2DUav(
    D3D12Core& core,
    const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

// format == DXGI_FORMAT_UNKNOWN のときは structured buffer view を作る。
// その場合 structureByteStride は必ず明示すること。
// format != DXGI_FORMAT_UNKNOWN のときは typed buffer view を作り、structureByteStride は 0 にする。
void CreateBufferSrv(
    D3D12Core& core,
    const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

// format == DXGI_FORMAT_UNKNOWN のときは structured buffer view を作る。
// その場合 structureByteStride は必ず明示すること。
// format != DXGI_FORMAT_UNKNOWN のときは typed buffer view を作り、structureByteStride は 0 にする。
void CreateBufferUav(
    D3D12Core& core,
    const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    ID3D12Resource* counterResource = nullptr);

void CreateConstantBufferView(
    D3D12Core& core,
    const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT64 byteOffset = 0,
    UINT sizeBytes = 0);

void CreateTexture2DRtv(
    D3D12Core& core,
    const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

void CreateTexture2DDsv(
    D3D12Core& core,
    const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

// --------------------------------------------------------------------------
// Sampler 作成ヘルパ
// --------------------------------------------------------------------------
D3D12_SAMPLER_DESC MakeLinearClampSamplerDesc();
D3D12_SAMPLER_DESC MakePointClampSamplerDesc();

void CreateSampler(
    D3D12Core& core,
    const D3D12_SAMPLER_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

} // namespace D3D12CoreLib
