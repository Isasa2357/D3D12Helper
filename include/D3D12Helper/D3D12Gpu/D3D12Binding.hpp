#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorHandle.hpp>

#include <cstdint>
#include <vector>

namespace D3D12CoreLib {

struct D3D12DescriptorHeapSet {
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    ID3D12DescriptorHeap* samplerHeap = nullptr;

    bool Empty() const noexcept { return !cbvSrvUavHeap && !samplerHeap; }
    UINT Count() const noexcept;
    void Bind(ID3D12GraphicsCommandList* commandList) const;
};

enum class D3D12RootBindingType {
    DescriptorTable,
    Constants32,
    ConstantBufferView,
    ShaderResourceView,
    UnorderedAccessView,
};

struct D3D12RootBinding {
    UINT rootIndex = UINT_MAX;
    D3D12RootBindingType type = D3D12RootBindingType::DescriptorTable;
    D3D12_GPU_DESCRIPTOR_HANDLE table = {};
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    std::vector<uint32_t> constants;
    UINT destOffset32BitValues = 0;
};

class D3D12BindingSet {
public:
    void Clear();
    bool Empty() const noexcept { return m_bindings.empty() && m_heaps.Empty(); }
    size_t BindingCount() const noexcept { return m_bindings.size(); }

    void SetDescriptorHeaps(const D3D12DescriptorHeapSet& heaps) noexcept { m_heaps = heaps; }
    const D3D12DescriptorHeapSet& DescriptorHeaps() const noexcept { return m_heaps; }

    void AddDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
    void AddDescriptorTable(UINT rootIndex, const D3D12DescriptorHandle& handle);
    void Add32BitConstants(UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues = 0);
    void AddConstantBufferView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
    void AddShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
    void AddUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);

    void BindCompute(ID3D12GraphicsCommandList* commandList) const;
    void BindGraphics(ID3D12GraphicsCommandList* commandList) const;

private:
    void AddAddressBinding(UINT rootIndex, D3D12RootBindingType type, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
    void Bind(ID3D12GraphicsCommandList* commandList, bool compute) const;

    D3D12DescriptorHeapSet m_heaps;
    std::vector<D3D12RootBinding> m_bindings;
};

void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, const D3D12DescriptorHeapSet& heaps);
void SetComputeDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
void SetGraphicsDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
void SetCompute32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues = 0);
void SetGraphics32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues = 0);

} // namespace D3D12CoreLib
