//
// test_SharedFence.cpp
//   共有フェンス（D3D12_FENCE_FLAG_SHARED）の NT ハンドル作成 / オープンと、
//   同一アダプタ上の 2 つの D3D12Core 間でのタイムライン共有を検証する。
//
// 本テストは D3D11 に依存せず D3D12 内で完結する（依存方向を保つ）。
// D3D11 との相互運用テストは上位ライブラリ D3DInterop 側で行う。
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12Fence.hpp>

using namespace D3D12CoreLib;

namespace {

// 同一アダプタ上に 2 つ目の D3D12Core を作る（WARP 許可・Debug OFF：CI 想定）。
std::shared_ptr<D3D12Core> MakeSecondCore(LUID luid) {
    D3D12CoreConfig cfg;
    cfg.enableDebugLayer    = false;
    cfg.enableGpuValidation = false;
    cfg.enableInfoQueue     = false;
    cfg.enableDred          = false;
    cfg.allowWarpAdapter    = true;
    cfg.createComputeQueue  = false;
    return D3D12Core::CreateSharedWithAdapterLuid(luid, cfg);
}

} // namespace

// A が共有フェンスを作り、B が NT ハンドルで開く。
// 同一タイムライン上で双方向に Signal → CpuWait できることを確認する。
TEST(SharedFence, D3D12ToD3D12RoundTrip) {
    REQUIRE_CORE(coreA);

    std::shared_ptr<D3D12Core> coreB;
    try {
        coreB = MakeSecondCore(coreA->GetAdapterLuid());
    } catch (const std::exception& e) {
        TEST_SKIP(std::string("cannot create 2nd D3D12 device: ") + e.what());
    }

    D3D12Fence fenceA;
    fenceA.InitializeShared(coreA->GetDevice());

    HANDLE h = fenceA.CreateSharedHandle(coreA->GetDevice());
    CHECK(h != nullptr);

    D3D12Fence fenceB;
    fenceB.OpenSharedHandle(coreB->GetDevice(), h);
    CloseHandle(h); // 開いた後はハンドルを閉じてよい（fence が参照を保持）。

    // A -> B : A のキューで値 1 を Signal し、B 側から CPU で待つ。
    fenceA.Signal(coreA->GetDirectCommandQueue(), 1);
    fenceB.Wait(1);
    CHECK(fenceB.GetCompletedValue() >= 1);

    // B -> A : 同じタイムラインに値 2 を Signal し、A 側から CPU で待つ。
    fenceB.Signal(coreB->GetDirectCommandQueue(), 2);
    fenceA.Wait(2);
    CHECK(fenceA.GetCompletedValue() >= 2);
}

// B のキューが「A が将来 Signal する値」を GPU 側で待つ（自己デッドロックしない向き）。
// 待ち値 1 は別デバイス A が Signal するため、B のキューは停止せず値 2 まで進める。
TEST(SharedFence, GpuWaitChainAcrossDevices) {
    REQUIRE_CORE(coreA);

    std::shared_ptr<D3D12Core> coreB;
    try {
        coreB = MakeSecondCore(coreA->GetAdapterLuid());
    } catch (const std::exception& e) {
        TEST_SKIP(std::string("cannot create 2nd D3D12 device: ") + e.what());
    }

    D3D12Fence fenceA;
    fenceA.InitializeShared(coreA->GetDevice());
    HANDLE h = fenceA.CreateSharedHandle(coreA->GetDevice());
    D3D12Fence fenceB;
    fenceB.OpenSharedHandle(coreB->GetDevice(), h);
    CloseHandle(h);

    // B のキュー: 値 1 を GPU 待ち → 続けて値 2 を Signal（この時点ではまだ 1 は未達）。
    fenceB.GpuWait(coreB->GetDirectCommandQueue(), 1);
    fenceB.Signal(coreB->GetDirectCommandQueue(), 2);

    // A: 値 1 を Signal して B の GpuWait を解除する。
    fenceA.Signal(coreA->GetDirectCommandQueue(), 1);

    // A: B が積んだ値 2 の完了を CPU で待つ。
    fenceA.Wait(2);
    CHECK(fenceA.GetCompletedValue() >= 2);
}
