#pragma once
//
// D3D12Fence.hpp
// Fence 値管理 / GPU 完了待ち / Queue 間同期。
// HANDLE を所有するため move-only（rule of five）。
//
// 共有フェンス（D3D11/D3D12 相互運用・複数デバイス間同期）:
//   InitializeShared() で D3D12_FENCE_FLAG_SHARED のフェンスを作り、
//   CreateSharedHandle() で NT ハンドルを発行、相手側（別 D3D12 デバイスの
//   OpenSharedHandle()、または D3D11 の ID3D11Device5::OpenSharedFence）で
//   開くと、同一タイムラインを共有できる。
//
// スレッド安全性:
//   m_nextValue を std::atomic<UINT64> にしているため、
//   あるスレッドが Signal() を呼びつつ別スレッドが IsCompleted() / GetCurrentValue()
//   を読む cross-thread アクセスは安全。
//   ただし Signal() 自体は1スレッドから呼ぶ想定（Queue を独占するスレッドが呼ぶ）。
//   Wait() / WaitIdle() は任意のスレッドから呼んでよい。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

#include <atomic>

namespace D3D12CoreLib {

class D3D12Fence {
public:
    D3D12Fence() = default;
    ~D3D12Fence();

    D3D12Fence(const D3D12Fence&)            = delete;
    D3D12Fence& operator=(const D3D12Fence&) = delete;

    // std::atomic は move できないため、明示的に実装する。
    D3D12Fence(D3D12Fence&& other) noexcept;
    D3D12Fence& operator=(D3D12Fence&& other) noexcept;

    void Initialize(ID3D12Device* device);

    // --- 共有フェンス（D3D11/D3D12 相互運用・複数デバイス間）------------------
    // D3D12_FENCE_FLAG_SHARED で作成する。相手 API で開くと同一タイムラインを共有する。
    void InitializeShared(ID3D12Device* device);

    // このフェンスの NT 共有ハンドルを作成する。呼び出し側が CloseHandle すること。
    // InitializeShared() で作成したフェンスに対してのみ有効。
    HANDLE CreateSharedHandle(ID3D12Device* device, LPCWSTR name = nullptr) const;

    // 相手が発行した共有フェンスハンドルを開く（同一アダプタ前提）。
    void OpenSharedHandle(ID3D12Device* device, HANDLE handle);

    // Queue を独占するスレッドから呼ぶ。Signal 値をアトミックにインクリメントして返す。
    UINT64 Signal(ID3D12CommandQueue* queue);

    // 任意のスレッドから呼んでよい。指定値の GPU 完了を CPU でブロック待ち。
    void Wait(UINT64 fenceValue);

    // Queue に Signal して、その完了まで待つ（フルフラッシュ）。
    void WaitIdle(ID3D12CommandQueue* queue);

    // --- 明示値での GPU Signal / GPU Wait（相互運用のプロデューサ/コンシューマ用）---
    // 共有タイムライン上の「明示値」で Signal / GpuWait する。値は上位の同期プロトコルが
    // 単調増加で管理する。内部の m_nextValue（自動インクリメント用）とは独立。
    //
    // 注意（自己デッドロック）: GpuWait(N) を積んだ「同じ Queue」に、その値を満たす
    // Signal(N) を後から積むと、GPU は投入順に処理するため永久停止する。GpuWait は
    // 必ず「別デバイス/別 Queue が将来 Signal する値」を待つこと
    // （相互運用・複数デバイス間同期の用途）。
    void Signal (ID3D12CommandQueue* queue, UINT64 value);
    void GpuWait(ID3D12CommandQueue* queue, UINT64 value);

    // 任意のスレッドから呼んでよい（ノンブロッキング）。
    bool   IsCompleted(UINT64 fenceValue) const;
    UINT64 GetCurrentValue()  const noexcept { return m_nextValue.load(); }
    UINT64 GetCompletedValue() const;
    ID3D12Fence* Get() const noexcept { return m_fence.Get(); }

private:
    void Destroy() noexcept;

    ComPtr<ID3D12Fence>      m_fence;
    HANDLE                   m_event     = nullptr;
    std::atomic<UINT64>      m_nextValue{ 1 }; // 0 は「未 Signal」を表すため 1 から開始
    bool                     m_isShared  = false;
};

} // namespace D3D12CoreLib
