//
// test_DescriptorAllocator.cpp - 線形ディスクリプタアロケータ
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(DescriptorAllocator, AllocateAndReset) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, /*shaderVisible*/ true);

    CHECK_EQ(alloc.GetCapacity(), 4u);
    CHECK_EQ(alloc.GetAllocatedCount(), 0u);

    D3D12DescriptorHandle h0 = alloc.Allocate();
    CHECK(h0.IsValid());
    CHECK_EQ(h0.index, 0u);
    CHECK(h0.shaderVisible);
    CHECK(h0.gpu.ptr != 0);              // shader-visible なので GPU ハンドル有効

    D3D12DescriptorHandle h1 = alloc.Allocate();
    CHECK_EQ(h1.index, 1u);
    CHECK_EQ(alloc.GetAllocatedCount(), 2u);

    alloc.Reset();
    CHECK_EQ(alloc.GetAllocatedCount(), 0u);
}

TEST(DescriptorAllocator, ExhaustionThrows) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    (void)alloc.Allocate();
    (void)alloc.Allocate();
    CHECK_THROWS(alloc.Allocate());       // 容量超過は例外
}

TEST(DescriptorAllocator, RtvNonShaderVisible) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, /*shaderVisible*/ false);
    D3D12DescriptorHandle h = alloc.Allocate();
    CHECK(h.IsValid());
    CHECK(!h.shaderVisible);
    CHECK(h.cpu.ptr != 0);
}
