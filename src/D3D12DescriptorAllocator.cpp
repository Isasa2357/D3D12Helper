//
// D3D12DescriptorAllocator.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12DescriptorAllocator.hpp>

#include <stdexcept>

namespace D3D12CoreLib {

void D3D12DescriptorAllocator::Initialize(ID3D12Device* device,
                                          D3D12_DESCRIPTOR_HEAP_TYPE type,
                                          UINT count, bool shaderVisible) {
    m_heap.Initialize(device, type, count, shaderVisible);
    m_allocated = 0;
}

D3D12DescriptorHandle D3D12DescriptorAllocator::Allocate() {
    const D3D12DescriptorRange range = AllocateRange(1);

    D3D12DescriptorHandle handle;
    handle.cpu = range.cpuStart;
    handle.gpu = range.gpuStart;
    handle.index = range.startIndex;
    handle.shaderVisible = range.shaderVisible;
    return handle;
}

D3D12DescriptorRange D3D12DescriptorAllocator::AllocateRange(UINT count) {
    if (count == 0) {
        throw std::runtime_error("D3D12DescriptorAllocator: count must be > 0");
    }
    if (count > m_heap.GetCapacity() - m_allocated) {
        throw std::runtime_error("D3D12DescriptorAllocator: out of descriptors");
    }

    const D3D12DescriptorHandle first = m_heap.GetHandle(m_allocated);

    D3D12DescriptorRange range;
    range.cpuStart = first.cpu;
    range.gpuStart = first.gpu;
    range.startIndex = m_allocated;
    range.count = count;
    range.descriptorSize = m_heap.GetDescriptorSize();
    range.shaderVisible = first.shaderVisible;

    m_allocated += count;
    return range;
}

} // namespace D3D12CoreLib
