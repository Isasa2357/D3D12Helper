#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace D3D12CoreLib;

namespace {

D3D12CpuImage MakeImage(UINT width, UINT height, UINT rowPitch, uint8_t base) {
    D3D12CpuImage img = CreateCpuImage(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, rowPitch);
    auto& p = img.planes[0];
    const UINT rowBytes = GetPackedRowPitch(width, img.format);
    for (UINT y = 0; y < height; ++y) {
        auto* row = img.pixels.data() + p.offsetBytes + static_cast<size_t>(y) * p.rowPitch;
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(x) * 4u;
            row[i + 0] = static_cast<uint8_t>(base + x + y * 17u);
            row[i + 1] = static_cast<uint8_t>(base + 40u + x + y * 13u);
            row[i + 2] = static_cast<uint8_t>(base + 80u + x + y * 7u);
            row[i + 3] = 255;
        }
        for (UINT i = rowBytes; i < p.rowPitch; ++i) row[i] = 0xCD;
    }
    return img;
}

std::vector<uint8_t> Packed(const D3D12CpuImage& img) {
    ValidateCpuImage(img, "Packed");
    const auto& p = img.planes[0];
    const UINT rowBytes = GetPackedRowPitch(img.width, img.format);
    return PackRows(img.pixels.data() + p.offsetBytes, p.rowPitch, rowBytes, p.height);
}

void CheckBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) CHECK_EQ(static_cast<int>(a[i]), static_cast<int>(b[i]));
}

} // namespace

TEST(Transfer, CpuImageLayout) {
    CHECK_EQ(GetPackedRowPitch(3, DXGI_FORMAT_R8G8B8A8_UNORM), 12u);
    CHECK_EQ(GetRequiredCpuImageSize(3, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 16), 32ull);

    auto img = CreateCpuImage(3, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 16);
    CHECK_EQ(img.PlaneCount(), 1u);
    CHECK_EQ(img.SizeBytes(), 32ull);
    CHECK_EQ(img.planes[0].rowPitch, 16u);

    const uint8_t src[10] = { 1, 2, 3, 4, 0xee, 5, 6, 7, 8, 0xee };
    const auto packed = PackRows(src, 5, 4, 2);
    CheckBytes(packed, std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8 });

    CHECK_THROWS(CreateCpuImage(0, 2, DXGI_FORMAT_R8G8B8A8_UNORM));
    CHECK_THROWS(CreateCpuImage(2, 2, DXGI_FORMAT_UNKNOWN));
    CHECK_THROWS(CreateCpuImage(2, 2, DXGI_FORMAT_BC1_UNORM));
    CHECK_THROWS(CreateCpuImage(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 15));
}

TEST(Transfer, TextureRoundTripAndUpdate) {
    REQUIRE_CORE(core);

    auto img = MakeImage(4, 3, 20, 10);
    auto tex = CreateTexture2DFromCpuImage(*core, img);
    auto got = ReadbackTexture2DToCpuImage(*core, tex);
    CheckBytes(Packed(got), Packed(img));

    auto updated = MakeImage(4, 3, 24, 70);
    UpdateTexture2DFromCpuImage(*core, tex, updated);
    auto got2 = ReadbackTexture2DToCpuImage(*core, tex);
    CheckBytes(Packed(got2), Packed(updated));
}

TEST(Transfer, RegionReadback) {
    REQUIRE_CORE(core);

    auto img = MakeImage(4, 3, 20, 30);
    auto tex = CreateTexture2DFromCpuImage(*core, img);

    D3D12_BOX box = { 1, 1, 0, 3, 3, 1 };
    auto region = ReadbackTexture2DRegionToCpuImage(*core, tex, box);
    CHECK_EQ(region.width, 2u);
    CHECK_EQ(region.height, 2u);

    std::vector<uint8_t> expected;
    const auto& p = img.planes[0];
    for (UINT y = 1; y < 3; ++y) {
        const auto* row = img.pixels.data() + p.offsetBytes + static_cast<size_t>(y) * p.rowPitch + 4u;
        expected.insert(expected.end(), row, row + 2u * 4u);
    }
    CheckBytes(Packed(region), expected);

    D3D12_BOX bad = { 1, 1, 0, 1, 3, 1 };
    CHECK_THROWS(ReadbackTexture2DRegionToCpuImage(*core, tex, bad));
    D3D12_BOX badDepth = { 0, 0, 0, 1, 1, 2 };
    CHECK_THROWS(ReadbackTexture2DRegionToCpuImage(*core, tex, badDepth));
}

TEST(Transfer, InvalidTextureInputs) {
    REQUIRE_CORE(core);
    D3D12Resource empty;
    CHECK_THROWS(ReadbackTexture2DToCpuImage(*core, empty));

    auto img = MakeImage(2, 2, 8, 1);
    auto tex = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK_THROWS(UpdateTexture2DFromCpuImage(*core, tex, img));
}
