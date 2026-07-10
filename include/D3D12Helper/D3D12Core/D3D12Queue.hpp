#pragma once
//
// D3D12Queue.hpp
// ID3D12CommandQueue とその専用 Fence を管理する。
// D3D12Fence が move-only のため、本クラスも move-only。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>
#include <D3D12Helper/D3D12Core/D3D12QueueSyncPoint.hpp>

namespace D3D12CoreLib {

class D3D12Queue {
public:
    D3D12Queue() = default;
    ~D3D12Queue() = default;

    D3D12Queue(const D3D12Queue&)            = delete;
    D3D12Queue& operator=(const D3D12Queue&) = delete;
    D3D12Queue(D3D12Queue&&)                 = default;
    D3D12Queue& operator=(D3D12Queue&&)      = default;

    void Initialize(
        ID3D12Device* device,
        D3D12_COMMAND_LIST_TYPE type,
        D3D12_COMMAND_QUEUE_PRIORITY priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

    ID3D12CommandQueue*     Get() const noexcept { return m_queue.Get(); }
    D3D12_COMMAND_LIST_TYPE GetType() const noexcept { return m_type; }
    D3D12Fence&             Fence() noexcept { return m_fence; }

    void ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists);

    // この Queue の Fence に Signal し、その値を返す。
    // v1.12.1 以前からの互換 API。
    UINT64 Signal();
    void   WaitForFenceValue(UINT64 value);
    void   WaitIdle();

    // 他 Queue の Fence 値完了を GPU 側で待つ（クロス Queue 同期）。
    // v1.12.1 以前からの互換 API。関数ポインタ取得を含むソース互換性を保つため、
    // SyncPoint 版は overload せず GpuWaitPoint という別名にする。
    void GpuWait(ID3D12Fence* fence, UINT64 value);

    // Fence と Signal 済み値をまとめた値型を返す追加 API。
    D3D12QueueSyncPoint SignalPoint();

    // point の Fence 完了を、この Queue の GPU 側で待つ。CPU はブロックしない。
    void GpuWaitPoint(const D3D12QueueSyncPoint& point);

    // point の Fence 完了まで CPU をブロックする。
    // Queue の状態は変更せず、point が指す Fence だけを待つ。
    void CpuWaitPoint(const D3D12QueueSyncPoint& point) const;

private:
    ComPtr<ID3D12CommandQueue> m_queue;
    D3D12Fence m_fence;
    D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
};

} // namespace D3D12CoreLib
