#include <D3D12Helper/D3D12Diagnostics/D3D12GpuTimer.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace D3D12CoreLib {
namespace {

void RequireCommandList(ID3D12GraphicsCommandList* commandList, const char* fn) {
    if (!commandList) throw std::runtime_error(std::string(fn) + ": null command list");
}

void RequireQueue(ID3D12CommandQueue* queue, const char* fn) {
    if (!queue) throw std::runtime_error(std::string(fn) + ": null command queue");
}

void RequireTimer(bool initialized, const char* fn) {
    if (!initialized) throw std::runtime_error(std::string(fn) + ": timer is not initialized");
}

} // namespace

void D3D12GpuTimer::Initialize(ID3D12Device* device) {
    if (!device) throw std::runtime_error("D3D12GpuTimer::Initialize: null device");
    Reset();
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2;
    D3D12CORE_THROW_IF_FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_queryHeap)));
    m_readback.Initialize(device, sizeof(UINT64) * 2u);
}

void D3D12GpuTimer::Reset() noexcept {
    m_queryHeap.Reset();
    m_readback = D3D12ReadbackBuffer{};
}

void D3D12GpuTimer::Begin(ID3D12GraphicsCommandList* commandList) {
    RequireCommandList(commandList, "D3D12GpuTimer::Begin");
    RequireTimer(IsInitialized(), "D3D12GpuTimer::Begin");
    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void D3D12GpuTimer::End(ID3D12GraphicsCommandList* commandList) {
    RequireCommandList(commandList, "D3D12GpuTimer::End");
    RequireTimer(IsInitialized(), "D3D12GpuTimer::End");
    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
}

void D3D12GpuTimer::Resolve(ID3D12GraphicsCommandList* commandList) {
    RequireCommandList(commandList, "D3D12GpuTimer::Resolve");
    RequireTimer(IsInitialized(), "D3D12GpuTimer::Resolve");
    commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_readback.Get(), 0);
}

double D3D12GpuTimer::GetElapsedMilliseconds(ID3D12CommandQueue* queue) {
    RequireQueue(queue, "D3D12GpuTimer::GetElapsedMilliseconds");
    RequireTimer(IsInitialized(), "D3D12GpuTimer::GetElapsedMilliseconds");
    UINT64 frequency = 0;
    D3D12CORE_THROW_IF_FAILED(queue->GetTimestampFrequency(&frequency));
    const auto* data = static_cast<const UINT64*>(m_readback.Map());
    const UINT64 begin = data[0];
    const UINT64 end = data[1];
    m_readback.Unmap();
    if (frequency == 0 || end <= begin) return 0.0;
    return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency);
}

void D3D12GpuProfiler::Initialize(ID3D12Device* device, UINT maxSections) {
    if (!device) throw std::runtime_error("D3D12GpuProfiler::Initialize: null device");
    if (maxSections == 0) throw std::runtime_error("D3D12GpuProfiler::Initialize: maxSections must be > 0");
    Reset();
    m_maxSections = maxSections;
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = maxSections * 2u;
    D3D12CORE_THROW_IF_FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_queryHeap)));
    m_readback.Initialize(device, sizeof(UINT64) * desc.Count);
}

void D3D12GpuProfiler::Reset() noexcept {
    m_queryHeap.Reset();
    m_readback = D3D12ReadbackBuffer{};
    m_maxSections = 0;
    m_names.clear();
}

UINT D3D12GpuProfiler::BeginSection(ID3D12GraphicsCommandList* commandList, const std::string& name) {
    RequireCommandList(commandList, "D3D12GpuProfiler::BeginSection");
    RequireTimer(IsInitialized(), "D3D12GpuProfiler::BeginSection");
    if (m_names.size() >= m_maxSections) throw std::runtime_error("D3D12GpuProfiler::BeginSection: too many sections");
    const UINT index = static_cast<UINT>(m_names.size());
    m_names.push_back(name);
    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2u);
    return index;
}

void D3D12GpuProfiler::EndSection(ID3D12GraphicsCommandList* commandList, UINT sectionIndex) {
    RequireCommandList(commandList, "D3D12GpuProfiler::EndSection");
    RequireTimer(IsInitialized(), "D3D12GpuProfiler::EndSection");
    if (sectionIndex >= m_names.size()) throw std::runtime_error("D3D12GpuProfiler::EndSection: invalid section index");
    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, sectionIndex * 2u + 1u);
}

void D3D12GpuProfiler::Resolve(ID3D12GraphicsCommandList* commandList) {
    RequireCommandList(commandList, "D3D12GpuProfiler::Resolve");
    RequireTimer(IsInitialized(), "D3D12GpuProfiler::Resolve");
    if (m_names.empty()) return;
    commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, static_cast<UINT>(m_names.size()) * 2u, m_readback.Get(), 0);
}

std::vector<D3D12GpuProfileResult> D3D12GpuProfiler::GetResults(ID3D12CommandQueue* queue) {
    RequireQueue(queue, "D3D12GpuProfiler::GetResults");
    RequireTimer(IsInitialized(), "D3D12GpuProfiler::GetResults");
    UINT64 frequency = 0;
    D3D12CORE_THROW_IF_FAILED(queue->GetTimestampFrequency(&frequency));
    std::vector<D3D12GpuProfileResult> out;
    out.reserve(m_names.size());
    const auto* data = static_cast<const UINT64*>(m_readback.Map());
    for (size_t i = 0; i < m_names.size(); ++i) {
        const UINT64 begin = data[i * 2u];
        const UINT64 end = data[i * 2u + 1u];
        D3D12GpuProfileResult r;
        r.name = m_names[i];
        if (frequency != 0 && end > begin) r.milliseconds = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency);
        out.push_back(std::move(r));
    }
    m_readback.Unmap();
    return out;
}

} // namespace D3D12CoreLib
