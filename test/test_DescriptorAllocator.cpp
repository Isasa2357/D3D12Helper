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

TEST(DescriptorAllocator, AllocateRange) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, /*shaderVisible*/ true);

    D3D12DescriptorRange r0 = alloc.AllocateRange(3);
    CHECK(r0.IsValid());
    CHECK_EQ(r0.startIndex, 0u);
    CHECK_EQ(r0.count, 3u);
    CHECK(r0.shaderVisible);
    CHECK(r0.gpuStart.ptr != 0);
    CHECK(r0.Cpu(1).ptr > r0.Cpu(0).ptr);
    CHECK(r0.Gpu(1).ptr > r0.Gpu(0).ptr);

    D3D12DescriptorRange r1 = alloc.AllocateRange(2);
    CHECK_EQ(r1.startIndex, 3u);
    CHECK_EQ(r1.count, 2u);
    CHECK_EQ(alloc.GetAllocatedCount(), 5u);
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

TEST(DescriptorAllocator, AllocateRangeThrows) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    CHECK_THROWS(alloc.AllocateRange(0));
    CHECK_THROWS(alloc.AllocateRange(3));
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
