#pragma once
//
// TestCommon.hpp
//
// デバイスを必要とするテスト向けの共通ヘルパ。
//   - TryMakeCore(): WARP 許可・Debug 層 OFF で D3D12Core を作る（CI で動くように）
//   - REQUIRE_CORE():  デバイスを作れない環境ではテストを SKIP
//   - REQUIRE_DXC():   dxcompiler.dll が無い環境ではテストを SKIP
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>

#include <memory>
#include <string>

namespace d3d12test {

// Debug 層や InfoQueue は CI に無いことがあるので無効化し、GPU 非搭載でも WARP で動かす。
inline std::shared_ptr<::D3D12CoreLib::D3D12Core> TryMakeCore(bool computeQueue = false) {
    ::D3D12CoreLib::D3D12CoreConfig cfg;
    cfg.enableDebugLayer    = false;
    cfg.enableGpuValidation = false;
    cfg.enableInfoQueue     = false;
    cfg.enableDred          = false;
    cfg.allowWarpAdapter    = true;
    cfg.createComputeQueue  = computeQueue;
    return ::D3D12CoreLib::D3D12Core::CreateShared(cfg);
}

} // namespace d3d12test

// デバイスを作り、失敗したら SKIP。以降 var を shared_ptr<D3D12Core> として使える。
#define REQUIRE_CORE(var)                                                         \
    std::shared_ptr<::D3D12CoreLib::D3D12Core> var;                               \
    try { var = ::d3d12test::TryMakeCore(); }                                     \
    catch (const std::exception& e) {                                             \
        TEST_SKIP(std::string("no usable D3D12 device: ") + e.what());            \
    }

// DXC が使えなければ SKIP（ランタイムに dxcompiler.dll が必要）。
#define REQUIRE_DXC()                                                             \
    do {                                                                          \
        try {                                                                     \
            (void)::D3D12CoreLib::CompileShaderFromSource_Dxc(                     \
                "RWBuffer<float> b : register(u0);"                               \
                "[numthreads(1,1,1)] void main(){ b[0] = 0; }",                   \
                "main", "cs_6_0");                                                \
        } catch (const std::exception& e) {                                       \
            TEST_SKIP(std::string("DXC unavailable: ") + e.what());               \
        }                                                                         \
    } while (0)
