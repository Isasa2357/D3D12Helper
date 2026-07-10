#pragma once
//
// D3D12BarrierBatch.hpp
// 明示的 before / after を保持する legacy Resource Barrier の集約型。
//
// 自動 state tracking、Command List への記録、Resource state の更新は行わない。
// 呼び出し側が Data() / Count() を任意の ResourceBarrier 対応 Command List に渡す。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

#include <cstddef>
#include <vector>

namespace D3D12CoreLib {

class D3D12BarrierBatch {
public:
    void Reserve(std::size_t count);

    // before == after の場合は追加せず false を返す。
    bool Transition(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    void Uav(ID3D12Resource* resource);

    // D3D12 の aliasing barrier 規則に従い、before / after は片方または両方 null を許可する。
    void Aliasing(ID3D12Resource* before, ID3D12Resource* after);

    const D3D12_RESOURCE_BARRIER* Data() const noexcept {
        return m_barriers.empty() ? nullptr : m_barriers.data();
    }

    UINT Count() const noexcept {
        return static_cast<UINT>(m_barriers.size());
    }

    bool Empty() const noexcept { return m_barriers.empty(); }

    const std::vector<D3D12_RESOURCE_BARRIER>& Items() const noexcept {
        return m_barriers;
    }

    void Clear() noexcept { m_barriers.clear(); }

private:
    std::vector<D3D12_RESOURCE_BARRIER> m_barriers;
};

} // namespace D3D12CoreLib
