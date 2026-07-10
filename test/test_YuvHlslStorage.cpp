//
// test_YuvHlslStorage.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasConversionShader(const std::filesystem::path& directory) {
    std::error_code ec;
    return std::filesystem::exists(directory / "ConvertRgbToNv12.hlsl", ec) && !ec &&
           std::filesystem::exists(directory / "YuvPrimitives.hlsli", ec) && !ec;
}

std::filesystem::path ProcessingShaderDirectory() {
    const auto runtime =
        std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasConversionShader(runtime)) {
        return runtime;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto source = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasConversionShader(source)) {
        return source;
    }
#endif

    return runtime;
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> value)
        : core(std::move(value)) {
        cbvSrvUav.Initialize(
            core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true);
        sampler.Initialize(
            core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDirectory());
    }
};

struct PlaneReadback {
    D3D12ReadbackBuffer buffer;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> layout = {};
    std::array<UINT, 2> rows = {};
    std::array<UINT64, 2> rowSize = {};
};

PlaneReadback ConvertUniformRedAndReadPlanes(
    ProcessingFixture& fixture,
    DXGI_FORMAT destinationFormat) {

    const std::vector<uint8_t> redRgba = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    auto source = CreateTexture2DFromRGBA(
        *fixture.core,
        redRgba.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fixture.context);
    auto destination = converter.CreateOutputTexture(
        *fixture.core,
        2,
        2,
        destinationFormat,
        D3D12_RESOURCE_STATE_COMMON);

    FormatConvertDesc convert = {};
    convert.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    convert.dstFormat = destinationFormat;
    convert.color.dstMatrix = ProcessingColorMatrix::BT709;
    convert.color.dstRange = ProcessingColorRange::Limited;

    PlaneReadback readback;
    const auto description = destination.GetDesc();
    UINT64 totalBytes = 0;
    fixture.core->GetDevice()->GetCopyableFootprints(
        &description,
        0,
        2,
        0,
        readback.layout.data(),
        readback.rows.data(),
        readback.rowSize.data(),
        &totalBytes);
    readback.buffer.Initialize(fixture.core->GetDevice(), totalBytes);

    auto context = fixture.core->CreateDirectContext();
    context.Reset();
    converter.RecordConvert(context, source, destination, convert);

    context.ResourceBarrier(MakeTransitionBarrier(
        destination.Get(),
        destination.GetState(),
        D3D12_RESOURCE_STATE_COPY_SOURCE));
    destination.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (UINT plane = 0; plane < 2; ++plane) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback.buffer.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = readback.layout[plane];

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = destination.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = plane;
        context.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    context.Close();
    ID3D12CommandList* lists[] = { context.GetCommandList() };
    fixture.core->DirectQueue().ExecuteCommandLists(1, lists);
    fixture.core->DirectQueue().WaitForFenceValue(fixture.core->DirectQueue().Signal());
    return readback;
}

uint16_t ReadU16(const std::byte* data) {
    uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

UINT64 PlaneMappedSize(const PlaneReadback& readback, UINT plane) {
    if (plane >= readback.layout.size()) {
        TEST_FAIL("PlaneMappedSize: plane index is out of range");
    }
    if (readback.rows[plane] == 0) {
        return 0;
    }

    const UINT64 rowPitch = readback.layout[plane].Footprint.RowPitch;
    const UINT64 rowBytes = readback.rowSize[plane];
    if (rowBytes > rowPitch) {
        TEST_FAIL("PlaneMappedSize: row size exceeds row pitch");
    }

    // GetCopyableFootprints reports totalBytes through the end of the final
    // populated row, not through the full padding of that row. Therefore the
    // readable range is (rows - 1) * rowPitch + rowBytes, not rows * rowPitch.
    return static_cast<UINT64>(readback.rows[plane] - 1u) * rowPitch + rowBytes;
}

} // namespace

TEST(YuvHlslPrimitives, Nv12StoreWritesBt709LimitedCodes) {
    REQUIRE_CORE(core);
    ProcessingFixture fixture(core);
    if (!fixture.context.SupportsNv12Uav()) {
        TEST_SKIP("NV12 UAV plane views are not supported");
    }

    auto readback = ConvertUniformRedAndReadPlanes(fixture, DXGI_FORMAT_NV12);

    {
        auto mapped = readback.buffer.MapRead(
            readback.layout[0].Offset,
            PlaneMappedSize(readback, 0));
        for (UINT row = 0; row < 2; ++row) {
            const auto* rowData = mapped.Data() +
                static_cast<size_t>(row) * readback.layout[0].Footprint.RowPitch;
            CHECK(static_cast<uint8_t>(rowData[0]) == 63u);
            CHECK(static_cast<uint8_t>(rowData[1]) == 63u);
        }
    }

    {
        auto mapped = readback.buffer.MapRead(
            readback.layout[1].Offset,
            PlaneMappedSize(readback, 1));
        CHECK(static_cast<uint8_t>(mapped.Data()[0]) == 102u);
        CHECK(static_cast<uint8_t>(mapped.Data()[1]) == 240u);
    }
}

TEST(YuvHlslPrimitives, P010StoreWritesHighTenBitCodes) {
    REQUIRE_CORE(core);
    ProcessingFixture fixture(core);
    if (!fixture.context.SupportsP010Uav()) {
        TEST_SKIP("P010 UAV plane views are not supported");
    }

    auto readback = ConvertUniformRedAndReadPlanes(fixture, DXGI_FORMAT_P010);

    {
        auto mapped = readback.buffer.MapRead(
            readback.layout[0].Offset,
            PlaneMappedSize(readback, 0));
        for (UINT row = 0; row < 2; ++row) {
            const auto* rowData = mapped.Data() +
                static_cast<size_t>(row) * readback.layout[0].Footprint.RowPitch;
            CHECK(ReadU16(rowData + 0) == static_cast<uint16_t>(250u << 6u));
            CHECK(ReadU16(rowData + 2) == static_cast<uint16_t>(250u << 6u));
        }
    }

    {
        auto mapped = readback.buffer.MapRead(
            readback.layout[1].Offset,
            PlaneMappedSize(readback, 1));
        CHECK(ReadU16(mapped.Data() + 0) == static_cast<uint16_t>(409u << 6u));
        CHECK(ReadU16(mapped.Data() + 2) == static_cast<uint16_t>(960u << 6u));
    }
}
