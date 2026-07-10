//
// test_CompatibilityV1121.cpp
// v1.12.1 で公開済みの主要シグネチャが一意なまま残っていることを確認する。
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Core/D3D12Queue.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ReadbackBuffer.hpp>

#include <type_traits>

using namespace D3D12CoreLib;

namespace {

using QueueSignalSignature = UINT64 (D3D12Queue::*)();
using QueueWaitForFenceSignature = void (D3D12Queue::*)(UINT64);
using QueueWaitIdleSignature = void (D3D12Queue::*)();
using QueueGpuWaitSignature = void (D3D12Queue::*)(ID3D12Fence*, UINT64);

using ReadbackInitializeSignature = void (D3D12ReadbackBuffer::*)(ID3D12Device*, UINT64);
using ReadbackMapSignature = const void* (D3D12ReadbackBuffer::*)();
using ReadbackUnmapSignature = void (D3D12ReadbackBuffer::*)();

using CreateBufferSignature = D3D12Resource (*)(
    D3D12Core&,
    UINT64,
    D3D12_HEAP_TYPE,
    D3D12_RESOURCE_STATES,
    D3D12_RESOURCE_FLAGS,
    D3D12_HEAP_FLAGS);

using CreateTexture2DSignature = D3D12Resource (*)(
    D3D12Core&,
    UINT,
    UINT,
    DXGI_FORMAT,
    D3D12_RESOURCE_STATES,
    D3D12_RESOURCE_FLAGS,
    UINT16,
    UINT16,
    D3D12_HEAP_FLAGS);

static_assert(std::is_same_v<decltype(&D3D12Queue::Signal), QueueSignalSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::WaitForFenceValue), QueueWaitForFenceSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::WaitIdle), QueueWaitIdleSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::GpuWait), QueueGpuWaitSignature>);

static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Initialize), ReadbackInitializeSignature>);
static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Map), ReadbackMapSignature>);
static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Unmap), ReadbackUnmapSignature>);

static_assert(std::is_same_v<decltype(&CreateBuffer), CreateBufferSignature>);
static_assert(std::is_same_v<decltype(&CreateTexture2D), CreateTexture2DSignature>);

static_assert(!std::is_copy_constructible_v<D3D12ReadbackBuffer>);
static_assert(!std::is_copy_assignable_v<D3D12ReadbackBuffer>);
static_assert(std::is_move_constructible_v<D3D12ReadbackBuffer>);
static_assert(std::is_move_assignable_v<D3D12ReadbackBuffer>);

} // namespace

TEST(CompatibilityV1121, PublicSignaturesCompile) {
    QueueSignalSignature signal = &D3D12Queue::Signal;
    QueueGpuWaitSignature gpuWait = &D3D12Queue::GpuWait;
    ReadbackMapSignature map = &D3D12ReadbackBuffer::Map;
    ReadbackUnmapSignature unmap = &D3D12ReadbackBuffer::Unmap;
    CreateBufferSignature createBuffer = &CreateBuffer;
    CreateTexture2DSignature createTexture2D = &CreateTexture2D;

    CHECK(signal != nullptr);
    CHECK(gpuWait != nullptr);
    CHECK(map != nullptr);
    CHECK(unmap != nullptr);
    CHECK(createBuffer != nullptr);
    CHECK(createTexture2D != nullptr);
}
