//
// test_ThrowIfFailed.cpp - HRESULT 例外化（デバイス不要）
//
#include "TestFramework.hpp"
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <string>

using namespace D3D12CoreLib;

TEST(ThrowIfFailed, SucceedDoesNotThrow) {
    ThrowIfFailed(S_OK);           // 例外を投げない
    ThrowIfFailed(S_OK, "ok");
}

TEST(ThrowIfFailed, FailThrows) {
    CHECK_THROWS(ThrowIfFailed(E_FAIL));
    CHECK_THROWS(ThrowIfFailed(E_INVALIDARG, "bad arg"));
}

TEST(ThrowIfFailed, MacroThrowsOnFailure) {
    CHECK_THROWS(D3D12CORE_THROW_IF_FAILED(E_FAIL));
}

TEST(ThrowIfFailed, HexStringContainsPrefix) {
    std::string s = HResultToHexString(E_FAIL);
    CHECK(s.find("0x") != std::string::npos);
}
