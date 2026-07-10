//
// D3D12BarrierBatch.cpp
//
#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>

#include <limits>
#include <stdexcept>

namespace D3D12CoreLib {

namespace {

void EnsureCanAppend(const std::vector<D3D12_RESOURCE_BARRIER>& barriers) {
    if (barriers.size() >= static_cast<std::size_t>((std::numeric_limits<UINT>::max)())) {
        throw std::overflow_error("D3D12BarrierBatch: barrier count exceeds UINT range");
    }
}

} // namespace

void D3D12BarrierBatch::Reserve(std::size_t count) {
    if (count > static_cast<std::size_t>((std::numeric_limits<UINT>::max)())) {
        throw std::overflow_error("D3D12BarrierBatch::Reserve: count exceeds UINT range");
    }
    m_barriers.reserve(count);
}

bool D3D12BarrierBatch::Transition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    UINT subresource) {

    if (!resource) {
        throw std::invalid_argument("D3D12BarrierBatch::Transition: null resource");
    }
    if (before == after) {
        return false;
    }

    EnsureCanAppend(m_barriers);
    m_barriers.push_back(MakeTransitionBarrier(resource, before, after, subresource));
    return true;
}

void D3D12BarrierBatch::Uav(ID3D12Resource* resource) {
    if (!resource) {
        throw std::invalid_argument("D3D12BarrierBatch::Uav: null resource");
    }

    EnsureCanAppend(m_barriers);
    m_barriers.push_back(MakeUavBarrier(resource));
}

void D3D12BarrierBatch::Aliasing(ID3D12Resource* before, ID3D12Resource* after) {
    EnsureCanAppend(m_barriers);
    m_barriers.push_back(MakeAliasingBarrier(before, after));
}

} // namespace D3D12CoreLib
