//
// D3D12CommandAllocatorContext.cpp
//
#include <D3D12Helper/D3D12Core/D3D12CommandAllocatorContext.hpp>

namespace D3D12CoreLib {

void D3D12CommandAllocatorContext::Initialize(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type) {

    if (!device) {
        throw std::invalid_argument(
            "D3D12CommandAllocatorContext::Initialize: device is null");
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    D3D12CORE_THROW_IF_FAILED_MSG(
        device->CreateCommandAllocator(
            type,
            IID_PPV_ARGS(allocator.GetAddressOf())),
        "D3D12CommandAllocatorContext::Initialize: CreateCommandAllocator failed");

    m_allocator = std::move(allocator);
    m_type = type;
}

void D3D12CommandAllocatorContext::Reset() {
    if (!m_allocator) {
        throw std::logic_error(
            "D3D12CommandAllocatorContext::Reset: context is not initialized");
    }

    D3D12CORE_THROW_IF_FAILED_MSG(
        m_allocator->Reset(),
        "D3D12CommandAllocatorContext::Reset: allocator reset failed; ensure all GPU work using it has completed");
}

} // namespace D3D12CoreLib
