#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Queue.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ReadbackBuffer.hpp>

#include <string>
#include <vector>

namespace D3D12CoreLib {

class D3D12GpuTimer {
public:
    void Initialize(ID3D12Device* device);
    void Reset() noexcept;
    bool IsInitialized() const noexcept { return m_queryHeap != nullptr; }

    void Begin(ID3D12GraphicsCommandList* commandList);
    void End(ID3D12GraphicsCommandList* commandList);
    void Resolve(ID3D12GraphicsCommandList* commandList);
    double GetElapsedMilliseconds(ID3D12CommandQueue* queue);

private:
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    D3D12ReadbackBuffer m_readback;
};

struct D3D12GpuProfileResult {
    std::string name;
    double milliseconds = 0.0;
};

class D3D12GpuProfiler {
public:
    void Initialize(ID3D12Device* device, UINT maxSections);
    void Reset() noexcept;
    bool IsInitialized() const noexcept { return m_queryHeap != nullptr; }

    UINT BeginSection(ID3D12GraphicsCommandList* commandList, const std::string& name);
    void EndSection(ID3D12GraphicsCommandList* commandList, UINT sectionIndex);
    void Resolve(ID3D12GraphicsCommandList* commandList);
    std::vector<D3D12GpuProfileResult> GetResults(ID3D12CommandQueue* queue);

private:
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    D3D12ReadbackBuffer m_readback;
    UINT m_maxSections = 0;
    std::vector<std::string> m_names;
};

} // namespace D3D12CoreLib
