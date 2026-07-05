//
// test_Subresource.cpp - D3D12 subresource index helper
//
#include "TestFramework.hpp"
#include "D3D12Core/D3D12Subresource.hpp"

using namespace D3D12CoreLib;

TEST(Subresource, CalcSubresource) {
    CHECK_EQ(CalcSubresource(0, 0, 0, 1, 1), 0u);
    CHECK_EQ(CalcSubresource(1, 0, 0, 4, 1), 1u);
    CHECK_EQ(CalcSubresource(0, 1, 0, 4, 3), 4u);
    CHECK_EQ(CalcSubresource(0, 0, 1, 4, 3), 12u);
    CHECK_EQ(CalcSubresource(2, 1, 1, 4, 3), 18u);
}
