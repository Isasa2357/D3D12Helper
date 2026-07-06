//
// test_DxgiUtil.cpp - LUID ユーティリティ（デバイス不要）
//
#include "TestFramework.hpp"
#include <D3D12Helper/D3D12Core/DxgiUtil.hpp>

using namespace D3D12CoreLib;

TEST(DxgiUtil, LuidEquals) {
    LUID a{}; a.LowPart = 123; a.HighPart = 45;
    LUID b{}; b.LowPart = 123; b.HighPart = 45;
    LUID c{}; c.LowPart = 999; c.HighPart = 45;
    CHECK(LuidEquals(a, b));
    CHECK(!LuidEquals(a, c));
}

TEST(DxgiUtil, LuidToWStringNonEmpty) {
    LUID a{}; a.LowPart = 7; a.HighPart = 8;
    std::wstring s = LuidToWString(a);
    CHECK(!s.empty());
}
