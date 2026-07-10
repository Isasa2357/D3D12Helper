//
// D3D12Queue.cpp
//
#include <D3D12Helper/D3D12Core/D3D12Queue.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {

namespace {

void ValidateSyncPoint(const D3D12QueueSyncPoint& point, const char* functionName) {
    if (!point.IsValid()) {
        throw std::runtime_error(std::string(functionName) + ": invalid sync point");
    }
}

} // namespace

void D3D12Queue::Initialize(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    D3D12_COMMAND_QUEUE_PRIORITY priority) {

    if (!device) throw std::runtime_error("D3D12Queue: null device");

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type     = type;
    desc.Priority = priority;
    desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    D3D12CORE_THROW_IF_FAILED(
        device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue)));

    m_type = type;
    m_fence.Initialize(device);
}

void D3D12Queue::ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists) {
    m_queue->ExecuteCommandLists(count, lists);
}

UINT64 D3D12Queue::Signal() {
    return m_fence.Signal(m_queue.Get());
}

void D3D12Queue::WaitForFenceValue(UINT64 value) {
    m_fence.Wait(value);
}

void D3D12Queue::WaitIdle() {
    m_fence.WaitIdle(m_queue.Get());
}

void D3D12Queue::GpuWait(ID3D12Fence* fence, UINT64 value) {
    D3D12CORE_THROW_IF_FAILED(m_queue->Wait(fence, value));
}

D3D12QueueSyncPoint D3D12Queue::SignalPoint() {
    const UINT64 value = Signal();
    return D3D12QueueSyncPoint(m_fence.Get(), value);
}

void D3D12Queue::GpuWait(const D3D12QueueSyncPoint& point) {
    ValidateSyncPoint(point, "D3D12Queue::GpuWait");
    GpuWait(point.GetFence(), point.GetValue());
}

void D3D12Queue::CpuWait(const D3D12QueueSyncPoint& point) const {
    ValidateSyncPoint(point, "D3D12Queue::CpuWait");

    ID3D12Fence* fence = point.GetFence();
    const UINT64 value = point.GetValue();
    if (fence->GetCompletedValue() >= value) {
        return;
    }

    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) {
        throw std::runtime_error("D3D12Queue::CpuWait: CreateEventW failed");
    }

    try {
        D3D12CORE_THROW_IF_FAILED(fence->SetEventOnCompletion(value, eventHandle));
        const DWORD waitResult = WaitForSingleObject(eventHandle, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            throw std::runtime_error("D3D12Queue::CpuWait: WaitForSingleObject failed");
        }
        CloseHandle(eventHandle);
    } catch (...) {
        CloseHandle(eventHandle);
        throw;
    }
}

} // namespace D3D12CoreLib
