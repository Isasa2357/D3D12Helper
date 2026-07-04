//
// test_UploadRing.cpp - リングバッファの確保・会計・回収
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(UploadRing, AllocateAndAccounting) {
    REQUIRE_CORE(core);
    D3D12UploadRing ring;
    const UINT64 ringSize = 1u << 20;   // 1 MB
    ring.Initialize(core->GetDevice(), ringSize);

    CHECK_EQ(ring.GetRingSize(), ringSize);
    CHECK_EQ(ring.GetUsedBytes(), 0ull);
    CHECK_EQ(ring.GetFreeBytes(), ringSize);

    D3D12UploadRing::Allocation a = ring.Allocate(256);
    CHECK(a.IsValid());
    CHECK(a.cpuPtr != nullptr);
    CHECK(a.resource != nullptr);
    CHECK_EQ(a.size, 256ull);
    CHECK(ring.GetUsedBytes() >= 256ull);
    CHECK(ring.GetUsedBytes() <= ringSize);
}

TEST(UploadRing, ReclaimFreesSpace) {
    REQUIRE_CORE(core);
    D3D12UploadRing ring;
    const UINT64 ringSize = 1u << 20;
    ring.Initialize(core->GetDevice(), ringSize);

    (void)ring.Allocate(4096);
    CHECK(ring.GetUsedBytes() > 0ull);

    // このフレームの確保を Fence 値に紐付け、完了後に回収する。
    UINT64 fv = core->DirectQueue().Signal();
    ring.FinishFrame(fv);
    core->DirectQueue().WaitForFenceValue(fv);
    ring.ReclaimCompleted(core->DirectQueue().Fence());

    // 回収後は再び確保できる空きがある。
    CHECK(ring.GetFreeBytes() >= 4096ull);
    CHECK(ring.GetUsedBytes() + ring.GetFreeBytes() <= ringSize + 4096ull);
}
