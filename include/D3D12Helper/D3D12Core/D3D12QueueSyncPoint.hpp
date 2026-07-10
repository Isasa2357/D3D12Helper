#pragma once
//
// D3D12QueueSyncPoint.hpp
// Fence と Signal 済み値を束ねる、Queue 間同期用の小さな値型。
//
// Video / Decode / Compute 等の用途固有型には依存しない。
// Fence は ComPtr で保持するため、生成元 Queue より後まで値型を保持しても
// Fence 自体がダングリングしない。Queue や Device の寿命は別途呼び出し側が管理する。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

class D3D12Queue;

class D3D12QueueSyncPoint {
public:
    D3D12QueueSyncPoint() = default;

    ID3D12Fence* GetFence() const noexcept { return m_fence.Get(); }
    UINT64 GetValue() const noexcept { return m_value; }

    bool IsValid() const noexcept {
        return m_fence != nullptr && m_value != 0;
    }

    explicit operator bool() const noexcept { return IsValid(); }

private:
    friend class D3D12Queue;

    D3D12QueueSyncPoint(ID3D12Fence* fence, UINT64 value) noexcept
        : m_value(value) {
        if (fence) {
            fence->AddRef();
            m_fence.Attach(fence);
        }
    }

    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_value = 0;
};

} // namespace D3D12CoreLib
