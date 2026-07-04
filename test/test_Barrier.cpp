//
// test_Barrier.cpp - Resource Barrier 生成ヘルパ（デバイス不要）
//
#include "TestFramework.hpp"
#include "D3D12Core/D3D12Barrier.hpp"

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
