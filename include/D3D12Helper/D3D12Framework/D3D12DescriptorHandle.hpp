#pragma once
//
// D3D12DescriptorHandle.hpp - CPU/GPU ディスクリプタハンドルのペア
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

#include <cassert>

namespace D3D12CoreLib {

struct D3D12DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    UINT index = 0;
    bool shaderVisible = false;

    bool IsValid() const noexcept { return cpu.ptr != 0; }
};

struct D3D12DescriptorRange {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
    UINT startIndex = 0;
    UINT count = 0;
    UINT descriptorSize = 0;
    bool shaderVisible = false;

    bool IsValid() const noexcept { return cpuStart.ptr != 0 && count > 0; }

    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const noexcept {
        assert(index < count);
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpuStart;
        h.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const noexcept {
        assert(shaderVisible);
        assert(index < count);
        D3D12_GPU_DESCRIPTOR_HANDLE h = gpuStart;
        h.ptr += static_cast<UINT64>(index) * descriptorSize;
        return h;
    }
};

} // namespace D3D12CoreLib
