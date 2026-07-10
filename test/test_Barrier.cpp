//
// test_Barrier.cpp - Resource Barrier 生成ヘルパ（デバイス不要）
//
#include "TestFramework.hpp"
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>

using namespace D3D12CoreLib;

TEST(Barrier, Transition) {
    D3D12_RESOURCE_BARRIER b = MakeTransitionBarrier(
        nullptr,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    CHECK(b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    CHECK(b.Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(b.Transition.StateAfter  == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // 既定では全サブリソースを対象にする。
    CHECK_EQ(b.Transition.Subresource, (UINT)D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
}

TEST(Barrier, TransitionSubresource) {
    D3D12_RESOURCE_BARRIER b = MakeTransitionBarrier(
        nullptr,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT,
        /*subresource*/ 2);
    CHECK_EQ(b.Transition.Subresource, 2u);
}

TEST(Barrier, Uav) {
    D3D12_RESOURCE_BARRIER b = MakeUavBarrier(nullptr);
    CHECK(b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV);
}

TEST(Barrier, Aliasing) {
    D3D12_RESOURCE_BARRIER b = MakeAliasingBarrier(nullptr, nullptr);
    CHECK(b.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING);
}

TEST(Barrier, BatchKeepsExplicitBarriers) {
    D3D12BarrierBatch batch;
    auto* resourceA = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(1));
    auto* resourceB = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(2));

    CHECK(batch.Empty());
    batch.Reserve(3);

    CHECK(batch.Transition(
        resourceA,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        2));
    batch.Uav(resourceB);
    batch.Aliasing(resourceA, resourceB);

    CHECK_EQ(batch.Count(), 3u);
    CHECK(batch.Data() != nullptr);
    CHECK(batch.Items()[0].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
    CHECK_EQ(batch.Items()[0].Transition.Subresource, 2u);
    CHECK(batch.Items()[1].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV);
    CHECK(batch.Items()[2].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING);

    batch.Clear();
    CHECK(batch.Empty());
    CHECK_EQ(batch.Count(), 0u);
    CHECK(batch.Data() == nullptr);
}

TEST(Barrier, BatchSkipsNoOpTransition) {
    D3D12BarrierBatch batch;
    auto* resource = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(1));

    CHECK(!batch.Transition(
        resource,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON));
    CHECK(batch.Empty());
}

TEST(Barrier, BatchRejectsNullTransitionAndUav) {
    D3D12BarrierBatch batch;
    CHECK_THROWS(batch.Transition(
        nullptr,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST));
    CHECK_THROWS(batch.Uav(nullptr));

    // Aliasing barrier は D3D12 仕様に従って null を許可する。
    CHECK_NOTHROW(batch.Aliasing(nullptr, nullptr));
    CHECK_EQ(batch.Count(), 1u);
}
