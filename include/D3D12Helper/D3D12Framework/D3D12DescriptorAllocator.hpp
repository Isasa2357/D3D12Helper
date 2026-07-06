#pragma once
//
// D3D12DescriptorAllocator.hpp
// 単純な線形 Allocator。Free はせず、必要なら Reset で巻き戻す。
//
// 所有方針: D3D12Core が単一所有するのではなく、サブシステムごとに必要なだけ
//           生成して使う。Reset のタイミングは各サブシステムが自分で管理すること。
//           Thread Safe ではない（必要なら呼び出し側で排他する）。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorHeap.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorHandle.hpp>

namespace D3D12CoreLib {

class D3D12DescriptorAllocator {
public:
    void Initialize(ID3D12Device* device,
                    D3D12_DESCRIPTOR_HEAP_TYPE type,
                    UINT count, bool shaderVisible);

    // 次のハンドルを1つ確保。容量超過時は例外。
    D3D12DescriptorHandle Allocate();

    // 連続するハンドル範囲を確保。容量超過時は例外。
    D3D12DescriptorRange AllocateRange(UINT count);

    // 確保位置を 0 に戻す（既存ハンドルは無効になる点に注意）。
    void Reset() noexcept { m_allocated = 0; }

    ID3D12DescriptorHeap* GetHeap() const noexcept { return m_heap.Get(); }
    const D3D12DescriptorHeap& Heap() const noexcept { return m_heap; }
    UINT GetAllocatedCount() const noexcept { return m_allocated; }
    UINT GetCapacity() const noexcept { return m_heap.GetCapacity(); }

private:
    D3D12DescriptorHeap m_heap;
    UINT m_allocated = 0;
};

} // namespace D3D12CoreLib
