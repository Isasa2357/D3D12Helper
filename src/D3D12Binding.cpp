#include <D3D12Helper/D3D12Gpu/D3D12Binding.hpp>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace D3D12CoreLib {
namespace {

void RequireCommandList(ID3D12GraphicsCommandList* commandList, const char* fn) {
    if (!commandList) throw std::runtime_error(std::string(fn) + ": null command list");
}

void RequireRootIndex(UINT rootIndex, const char* fn) {
    if (rootIndex == UINT_MAX) throw std::runtime_error(std::string(fn) + ": invalid root index");
}

void RequireGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle, const char* fn) {
    if (handle.ptr == 0) throw std::runtime_error(std::string(fn) + ": invalid GPU descriptor handle");
}

void RequireGpuAddress(D3D12_GPU_VIRTUAL_ADDRESS address, const char* fn) {
    if (address == 0) throw std::runtime_error(std::string(fn) + ": invalid GPU virtual address");
}

} // namespace

UINT D3D12DescriptorHeapSet::Count() const noexcept {
    return static_cast<UINT>((cbvSrvUavHeap ? 1 : 0) + (samplerHeap ? 1 : 0));
}

void D3D12DescriptorHeapSet::Bind(ID3D12GraphicsCommandList* commandList) const {
    SetDescriptorHeaps(commandList, *this);
}

void SetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, const D3D12DescriptorHeapSet& heaps) {
    RequireCommandList(commandList, "SetDescriptorHeaps");
    ID3D12DescriptorHeap* raw[2] = {};
    UINT count = 0;
    if (heaps.cbvSrvUavHeap) raw[count++] = heaps.cbvSrvUavHeap;
    if (heaps.samplerHeap) raw[count++] = heaps.samplerHeap;
    if (count > 0) commandList->SetDescriptorHeaps(count, raw);
}

void SetComputeDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    RequireCommandList(commandList, "SetComputeDescriptorTable");
    RequireRootIndex(rootIndex, "SetComputeDescriptorTable");
    RequireGpuHandle(gpuHandle, "SetComputeDescriptorTable");
    commandList->SetComputeRootDescriptorTable(rootIndex, gpuHandle);
}

void SetGraphicsDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    RequireCommandList(commandList, "SetGraphicsDescriptorTable");
    RequireRootIndex(rootIndex, "SetGraphicsDescriptorTable");
    RequireGpuHandle(gpuHandle, "SetGraphicsDescriptorTable");
    commandList->SetGraphicsRootDescriptorTable(rootIndex, gpuHandle);
}

void SetCompute32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues) {
    RequireCommandList(commandList, "SetCompute32BitConstants");
    RequireRootIndex(rootIndex, "SetCompute32BitConstants");
    if (!data) throw std::runtime_error("SetCompute32BitConstants: null data");
    if (num32BitValues == 0) throw std::runtime_error("SetCompute32BitConstants: num32BitValues must be > 0");
    commandList->SetComputeRoot32BitConstants(rootIndex, num32BitValues, data, destOffset32BitValues);
}

void SetGraphics32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues) {
    RequireCommandList(commandList, "SetGraphics32BitConstants");
    RequireRootIndex(rootIndex, "SetGraphics32BitConstants");
    if (!data) throw std::runtime_error("SetGraphics32BitConstants: null data");
    if (num32BitValues == 0) throw std::runtime_error("SetGraphics32BitConstants: num32BitValues must be > 0");
    commandList->SetGraphicsRoot32BitConstants(rootIndex, num32BitValues, data, destOffset32BitValues);
}

void D3D12BindingSet::Clear() {
    m_heaps = {};
    m_bindings.clear();
}

void D3D12BindingSet::AddDescriptorTable(UINT rootIndex, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    RequireRootIndex(rootIndex, "D3D12BindingSet::AddDescriptorTable");
    RequireGpuHandle(gpuHandle, "D3D12BindingSet::AddDescriptorTable");
    D3D12RootBinding b;
    b.rootIndex = rootIndex;
    b.type = D3D12RootBindingType::DescriptorTable;
    b.table = gpuHandle;
    m_bindings.push_back(std::move(b));
}

void D3D12BindingSet::AddDescriptorTable(UINT rootIndex, const D3D12DescriptorHandle& handle) {
    if (!handle.shaderVisible || handle.gpu.ptr == 0) {
        throw std::runtime_error("D3D12BindingSet::AddDescriptorTable: handle is not shader-visible");
    }
    AddDescriptorTable(rootIndex, handle.gpu);
}

void D3D12BindingSet::Add32BitConstants(UINT rootIndex, const void* data, UINT num32BitValues, UINT destOffset32BitValues) {
    RequireRootIndex(rootIndex, "D3D12BindingSet::Add32BitConstants");
    if (!data) throw std::runtime_error("D3D12BindingSet::Add32BitConstants: null data");
    if (num32BitValues == 0) throw std::runtime_error("D3D12BindingSet::Add32BitConstants: num32BitValues must be > 0");
    D3D12RootBinding b;
    b.rootIndex = rootIndex;
    b.type = D3D12RootBindingType::Constants32;
    b.constants.resize(num32BitValues);
    std::memcpy(b.constants.data(), data, static_cast<size_t>(num32BitValues) * sizeof(uint32_t));
    b.destOffset32BitValues = destOffset32BitValues;
    m_bindings.push_back(std::move(b));
}

void D3D12BindingSet::AddAddressBinding(UINT rootIndex, D3D12RootBindingType type, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {
    RequireRootIndex(rootIndex, "D3D12BindingSet::AddAddressBinding");
    RequireGpuAddress(gpuAddress, "D3D12BindingSet::AddAddressBinding");
    D3D12RootBinding b;
    b.rootIndex = rootIndex;
    b.type = type;
    b.gpuAddress = gpuAddress;
    m_bindings.push_back(std::move(b));
}

void D3D12BindingSet::AddConstantBufferView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {
    AddAddressBinding(rootIndex, D3D12RootBindingType::ConstantBufferView, gpuAddress);
}

void D3D12BindingSet::AddShaderResourceView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {
    AddAddressBinding(rootIndex, D3D12RootBindingType::ShaderResourceView, gpuAddress);
}

void D3D12BindingSet::AddUnorderedAccessView(UINT rootIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {
    AddAddressBinding(rootIndex, D3D12RootBindingType::UnorderedAccessView, gpuAddress);
}

void D3D12BindingSet::BindCompute(ID3D12GraphicsCommandList* commandList) const { Bind(commandList, true); }
void D3D12BindingSet::BindGraphics(ID3D12GraphicsCommandList* commandList) const { Bind(commandList, false); }

void D3D12BindingSet::Bind(ID3D12GraphicsCommandList* commandList, bool compute) const {
    RequireCommandList(commandList, compute ? "D3D12BindingSet::BindCompute" : "D3D12BindingSet::BindGraphics");
    SetDescriptorHeaps(commandList, m_heaps);
    for (const auto& b : m_bindings) {
        switch (b.type) {
        case D3D12RootBindingType::DescriptorTable:
            if (compute) commandList->SetComputeRootDescriptorTable(b.rootIndex, b.table);
            else commandList->SetGraphicsRootDescriptorTable(b.rootIndex, b.table);
            break;
        case D3D12RootBindingType::Constants32:
            if (compute) commandList->SetComputeRoot32BitConstants(b.rootIndex, static_cast<UINT>(b.constants.size()), b.constants.data(), b.destOffset32BitValues);
            else commandList->SetGraphicsRoot32BitConstants(b.rootIndex, static_cast<UINT>(b.constants.size()), b.constants.data(), b.destOffset32BitValues);
            break;
        case D3D12RootBindingType::ConstantBufferView:
            if (compute) commandList->SetComputeRootConstantBufferView(b.rootIndex, b.gpuAddress);
            else commandList->SetGraphicsRootConstantBufferView(b.rootIndex, b.gpuAddress);
            break;
        case D3D12RootBindingType::ShaderResourceView:
            if (compute) commandList->SetComputeRootShaderResourceView(b.rootIndex, b.gpuAddress);
            else commandList->SetGraphicsRootShaderResourceView(b.rootIndex, b.gpuAddress);
            break;
        case D3D12RootBindingType::UnorderedAccessView:
            if (compute) commandList->SetComputeRootUnorderedAccessView(b.rootIndex, b.gpuAddress);
            else commandList->SetGraphicsRootUnorderedAccessView(b.rootIndex, b.gpuAddress);
            break;
        }
    }
}

} // namespace D3D12CoreLib
