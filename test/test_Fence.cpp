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

TEST(Fence, LegacyGpuWaitSignatureRemainsUnique) {
    using LegacyGpuWait = void (D3D12Queue::*)(ID3D12Fence*, UINT64);
    LegacyGpuWait legacyGpuWait = &D3D12Queue::GpuWait;
    CHECK(legacyGpuWait != nullptr);
}

TEST(Fence, SyncPointSignalsAndCpuWaits) {
    REQUIRE_CORE(core);
    D3D12Queue& q = core->DirectQueue();

    D3D12QueueSyncPoint point = q.SignalPoint();
    CHECK(point.IsValid());
    CHECK(point.GetFence() != nullptr);
    CHECK(point.GetValue() != 0);

    q.CpuWaitPoint(point);
    CHECK(point.GetFence()->GetCompletedValue() >= point.GetValue());
}

TEST(Fence, SyncPointCrossQueueGpuWait) {
    REQUIRE_CORE(core);

    D3D12Queue& producer = core->DirectQueue();
    D3D12Queue& consumer = core->CopyQueue();

    const D3D12QueueSyncPoint produced = producer.SignalPoint();
    consumer.GpuWaitPoint(produced);

    const D3D12QueueSyncPoint consumed = consumer.SignalPoint();
    consumer.CpuWaitPoint(consumed);

    CHECK(consumed.GetFence()->GetCompletedValue() >= consumed.GetValue());
}

TEST(Fence, SyncPointRejectsInvalidValue) {
    REQUIRE_CORE(core);
    D3D12QueueSyncPoint invalid;

    CHECK_THROWS(core->DirectQueue().GpuWaitPoint(invalid));
    CHECK_THROWS(core->DirectQueue().CpuWaitPoint(invalid));
}
