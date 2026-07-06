//
// test_FormatUtil.cpp - FormatUtil のフォーマット判定（デバイス不要）
//
#include "TestFramework.hpp"
#include <D3D12Helper/D3D12Core/D3D12FormatUtil.hpp>

using namespace D3D12CoreLib;

TEST(FormatUtil, BytesAndBitsPerPixel) {
    CHECK_EQ(FormatUtil::BytesPerPixel(DXGI_FORMAT_R8G8B8A8_UNORM), 4u);
    CHECK_EQ(FormatUtil::BitsPerPixel(DXGI_FORMAT_R8G8B8A8_UNORM), 32u);
    CHECK_EQ(FormatUtil::BytesPerPixel(DXGI_FORMAT_R16G16B16A16_FLOAT), 8u);
    CHECK_EQ(FormatUtil::BytesPerPixel(DXGI_FORMAT_R32_FLOAT), 4u);
    CHECK_EQ(FormatUtil::BytesPerPixel(DXGI_FORMAT_R8_UNORM), 1u);
}

TEST(FormatUtil, DepthFormats) {
    CHECK(FormatUtil::IsDepthFormat(DXGI_FORMAT_D32_FLOAT));
    CHECK(FormatUtil::IsDepthFormat(DXGI_FORMAT_D24_UNORM_S8_UINT));
    CHECK(!FormatUtil::IsDepthFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
}

TEST(FormatUtil, BlockCompressed) {
    CHECK(FormatUtil::IsBlockCompressedFormat(DXGI_FORMAT_BC1_UNORM));
    CHECK(!FormatUtil::IsBlockCompressedFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
    // ブロック圧縮は px 単位のバイト数を返せない（0 を返す約束）。
    CHECK_EQ(FormatUtil::BytesPerPixel(DXGI_FORMAT_BC1_UNORM), 0u);
}

TEST(FormatUtil, Typeless) {
    CHECK(FormatUtil::IsTypelessFormat(DXGI_FORMAT_R8G8B8A8_TYPELESS));
    CHECK(!FormatUtil::IsTypelessFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
}

TEST(FormatUtil, YuvAndPlanarFormats) {
    CHECK(FormatUtil::IsYuvFormat(DXGI_FORMAT_NV12));
    CHECK(FormatUtil::IsYuvFormat(DXGI_FORMAT_YUY2));
    CHECK(!FormatUtil::IsYuvFormat(DXGI_FORMAT_R8G8B8A8_UNORM));

    CHECK(FormatUtil::IsPlanarFormat(DXGI_FORMAT_NV12));
    CHECK(FormatUtil::IsPlanarFormat(DXGI_FORMAT_P010));
    CHECK(!FormatUtil::IsPlanarFormat(DXGI_FORMAT_YUY2));
    CHECK(!FormatUtil::IsPlanarFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
}

TEST(FormatUtil, PlaneCountAndEvenSize) {
    CHECK_EQ(FormatUtil::GetKnownPlaneCount(DXGI_FORMAT_UNKNOWN), 0u);
    CHECK_EQ(FormatUtil::GetKnownPlaneCount(DXGI_FORMAT_R8G8B8A8_UNORM), 1u);
    CHECK_EQ(FormatUtil::GetKnownPlaneCount(DXGI_FORMAT_NV12), 2u);
    CHECK_EQ(FormatUtil::GetKnownPlaneCount(DXGI_FORMAT_P010), 2u);

    CHECK(FormatUtil::RequiresEvenSize(DXGI_FORMAT_NV12));
    CHECK(FormatUtil::RequiresEvenSize(DXGI_FORMAT_P010));
    CHECK(!FormatUtil::RequiresEvenSize(DXGI_FORMAT_R8G8B8A8_UNORM));
}
