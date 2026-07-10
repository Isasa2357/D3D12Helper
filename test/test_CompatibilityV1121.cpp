//
// test_CompatibilityV1121.cpp
// v1.12.1 で公開済みの主要シグネチャが一意なまま残っていることを確認する。
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Core/D3D12Queue.hpp>
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

static_assert(std::is_same_v<decltype(&D3D12Queue::Signal), QueueSignalSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::WaitForFenceValue), QueueWaitForFenceSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::WaitIdle), QueueWaitIdleSignature>);
static_assert(std::is_same_v<decltype(&D3D12Queue::GpuWait), QueueGpuWaitSignature>);

static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Initialize), ReadbackInitializeSignature>);
static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Map), ReadbackMapSignature>);
static_assert(std::is_same_v<decltype(&D3D12ReadbackBuffer::Unmap), ReadbackUnmapSignature>);

static_assert(!std::is_copy_constructible_v<D3D12ReadbackBuffer>);
static_assert(!std::is_copy_assignable_v<D3D12ReadbackBuffer>);
static_assert(std::is_move_constructible_v<D3D12ReadbackBuffer>);
static_assert(std::is_move_assignable_v<D3D12ReadbackBuffer>);

} // namespace

TEST(Hardening, CompatibilityV1121PublicSignaturesCompile) {
    QueueSignalSignature signal = &D3D12Queue::Signal;
    QueueGpuWaitSignature gpuWait = &D3D12Queue::GpuWait;
    ReadbackMapSignature map = &D3D12ReadbackBuffer::Map;
    ReadbackUnmapSignature unmap = &D3D12ReadbackBuffer::Unmap;

    CHECK(signal != nullptr);
    CHECK(gpuWait != nullptr);
    CHECK(map != nullptr);
    CHECK(unmap != nullptr);
}
