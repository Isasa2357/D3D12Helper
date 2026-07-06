//
// D3D12Fence.cpp
//
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>

namespace D3D12CoreLib {

D3D12Fence::~D3D12Fence() {
    Destroy();
}

void D3D12Fence::Destroy() noexcept {
    if (m_event) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    m_fence.Reset();
    m_nextValue.store(1);
    m_isShared = false;
}

D3D12Fence::D3D12Fence(D3D12Fence&& other) noexcept
    : m_fence(std::move(other.m_fence))
    , m_event(other.m_event)
    , m_nextValue(other.m_nextValue.load()) // atomic は move 不可なので load で値を取り出す
    , m_isShared(other.m_isShared)
{
    other.m_event = nullptr;
    other.m_nextValue.store(1);
    other.m_isShared = false;
}

D3D12Fence& D3D12Fence::operator=(D3D12Fence&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_fence     = std::move(other.m_fence);
        m_event     = other.m_event;
        m_nextValue.store(other.m_nextValue.load());
        m_isShared  = other.m_isShared;
        other.m_event = nullptr;
        other.m_nextValue.store(1);
        other.m_isShared = false;
    }
    return *this;
}

void D3D12Fence::Initialize(ID3D12Device* device) {
    Destroy();
    if (!device) throw std::runtime_error("D3D12Fence: null device");

    D3D12CORE_THROW_IF_FAILED(
        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

    m_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (m_event == nullptr) {
        D3D12CORE_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }
    m_nextValue.store(1);
    m_isShared = false;
}

void D3D12Fence::InitializeShared(ID3D12Device* device) {
    Destroy();
    if (!device) throw std::runtime_error("D3D12Fence::InitializeShared: null device");

    D3D12CORE_THROW_IF_FAILED(
        device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence)));

    m_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (m_event == nullptr) {
        D3D12CORE_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }
    m_nextValue.store(1);
    m_isShared = true;
}

HANDLE D3D12Fence::CreateSharedHandle(ID3D12Device* device, LPCWSTR name) const {
    if (!device || !m_fence) {
        throw std::runtime_error("D3D12Fence::CreateSharedHandle: not initialized");
    }
    if (!m_isShared) {
        throw std::runtime_error("D3D12Fence::CreateSharedHandle: fence was not created as shared");
    }
    HANDLE handle = nullptr;
    // フェンスは InitializeShared（D3D12_FENCE_FLAG_SHARED）で作られている必要がある。
    D3D12CORE_THROW_IF_FAILED(
        device->CreateSharedHandle(m_fence.Get(), nullptr, GENERIC_ALL, name, &handle));
    return handle;
}

void D3D12Fence::OpenSharedHandle(ID3D12Device* device, HANDLE handle) {
    Destroy();
    if (!device) throw std::runtime_error("D3D12Fence::OpenSharedHandle: null device");
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("D3D12Fence::OpenSharedHandle: null handle");
    }

    D3D12CORE_THROW_IF_FAILED(
        device->OpenSharedHandle(handle, IID_PPV_ARGS(&m_fence)));

    m_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (m_event == nullptr) {
        D3D12CORE_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }
    // 開いたフェンスは共有タイムライン上の「明示値」で運用する想定のため、
    // 自動インクリメント用 m_nextValue は既定値に戻しておく。
    m_nextValue.store(1);
    m_isShared = true;
}

UINT64 D3D12Fence::Signal(ID3D12CommandQueue* queue) {
    // fetch_add はアトミックにインクリメントし、元の値を返す。
    // Queue を独占するスレッドから呼ぶ想定だが、
    // 他スレッドが同時に IsCompleted / GetCurrentValue を読んでも安全。
    const UINT64 value = m_nextValue.fetch_add(1);
    D3D12CORE_THROW_IF_FAILED(queue->Signal(m_fence.Get(), value));
    return value;
}

void D3D12Fence::Signal(ID3D12CommandQueue* queue, UINT64 value) {
    if (!queue || !m_fence) {
        throw std::runtime_error("D3D12Fence::Signal(value): not initialized");
    }
    D3D12CORE_THROW_IF_FAILED(queue->Signal(m_fence.Get(), value));
}

void D3D12Fence::GpuWait(ID3D12CommandQueue* queue, UINT64 value) {
    if (!queue || !m_fence) {
        throw std::runtime_error("D3D12Fence::GpuWait: not initialized");
    }
    // GPU タイムライン上で value を待つ。値を Signal するのは別デバイス/別 Queue であること。
    D3D12CORE_THROW_IF_FAILED(queue->Wait(m_fence.Get(), value));
}

void D3D12Fence::Wait(UINT64 fenceValue) {
    // GetCompletedValue / SetEventOnCompletion は D3D12 がスレッドセーフ保証。
    // 任意のスレッドから呼んでよい。
    if (m_fence->GetCompletedValue() < fenceValue) {
        D3D12CORE_THROW_IF_FAILED(
            m_fence->SetEventOnCompletion(fenceValue, m_event));
        WaitForSingleObject(m_event, INFINITE);
    }
}

void D3D12Fence::WaitIdle(ID3D12CommandQueue* queue) {
    Wait(Signal(queue));
}

bool D3D12Fence::IsCompleted(UINT64 fenceValue) const {
    // GetCompletedValue は D3D12 がスレッドセーフ保証。
    return m_fence->GetCompletedValue() >= fenceValue;
}

UINT64 D3D12Fence::GetCompletedValue() const {
    return m_fence ? m_fence->GetCompletedValue() : 0;
}

} // namespace D3D12CoreLib
