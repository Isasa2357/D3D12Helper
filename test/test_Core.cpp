//
// test_Core.cpp - D3D12Core ファサードの初期化とアクセサ
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(Core, CreateAndBasics) {
    REQUIRE_CORE(core);
    CHECK(core->GetDevice() != nullptr);
    CHECK(core->GetDirectCommandQueue() != nullptr);
    // Direct キューは常に作られる。既定では Copy も作られる。
    CHECK(core->DirectQueue().GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT);
    CHECK(core->CopyQueue().GetType() == D3D12_COMMAND_LIST_TYPE_COPY);
    // アダプタ名は空でない。
    CHECK(!core->DeviceContext().GetAdapterName().empty());
}

TEST(Core, AdapterLuidRoundTrip) {
    REQUIRE_CORE(core);
    LUID luid = core->GetAdapterLuid();
    CHECK(core->IsSameAdapter(luid));

    LUID other = luid;
    other.LowPart = ~other.LowPart;   // 別アダプタ相当
    CHECK(!core->IsSameAdapter(other));
}

TEST(Core, ComputeQueueIsOptional) {
    // 既定では専用 Compute キューは作られない。
    std::shared_ptr<D3D12Core> core;
    try { core = ::d3d12test::TryMakeCore(/*computeQueue*/ false); }
    catch (const std::exception& e) { TEST_SKIP(std::string("no device: ") + e.what()); }
    CHECK(core->ComputeQueue() == nullptr);

    // 要求すれば作られる。
    std::shared_ptr<D3D12Core> core2;
    try { core2 = ::d3d12test::TryMakeCore(/*computeQueue*/ true); }
    catch (const std::exception& e) { TEST_SKIP(std::string("no device: ") + e.what()); }
    CHECK(core2->ComputeQueue() != nullptr);
    CHECK(core2->ComputeQueue()->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);
}

TEST(Core, WaitIdleDoesNotThrow) {
    REQUIRE_CORE(core);
    core->WaitIdle();   // 何も積んでいなくても安全にフラッシュできる
}
