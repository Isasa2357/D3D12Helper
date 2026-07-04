//
// test_Fence.cpp - D3D12Queue が持つ専用 Fence の値管理・完了待ち
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(Fence, SignalIncrementsAndCompletes) {
    REQUIRE_CORE(core);
    D3D12Queue& q = core->DirectQueue();

    UINT64 a = q.Signal();
    UINT64 b = q.Signal();
    CHECK(b > a);

    q.WaitForFenceValue(b);
    CHECK(q.Fence().IsCompleted(b));
    CHECK(q.Fence().GetCompletedValue() >= b);
}

TEST(Fence, WaitIdleFlushes) {
    REQUIRE_CORE(core);
    D3D12Queue& q = core->DirectQueue();
    UINT64 before = q.Signal();
    q.WaitIdle();
    CHECK(q.Fence().GetCompletedValue() >= before);
}
