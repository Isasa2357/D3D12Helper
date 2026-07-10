//
// test_UploadReadback.cpp - UploadBuffer -> GPU -> ReadbackBuffer の往復
//
#include "TestCommon.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace D3D12CoreLib;

TEST(UploadReadback, BufferRoundTrip) {
    REQUIRE_CORE(core);
    ID3D12Device* device = core->GetDevice();

    const UINT count = 256;
    const UINT64 bytes = static_cast<UINT64>(count) * sizeof(uint32_t);

    // 送るデータ
    std::vector<uint32_t> src(count);
    for (UINT i = 0; i < count; ++i) src[i] = i * 7u + 1u;

    // Upload -> DEFAULT -> Readback
    D3D12UploadBuffer upload;
    upload.Initialize(device, bytes);
    std::memcpy(upload.Map(), src.data(), bytes);

    D3D12Resource dst = CreateBuffer(
        *core, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12ReadbackBuffer readback;
    readback.Initialize(device, bytes);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    auto* cl = ctx.GetCommandList();

    cl->CopyBufferRegion(dst.Get(), 0, upload.Get(), 0, bytes);

    ctx.ResourceBarrier(MakeTransitionBarrier(
        dst.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE));

    cl->CopyBufferRegion(readback.Get(), 0, dst.Get(), 0, bytes);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

    // v1.12.1 以前からの Map / Unmap 経路を維持する。
    const auto* got = static_cast<const uint32_t*>(readback.Map());
    int mismatches = 0;
    for (UINT i = 0; i < count; ++i) {
        if (got[i] != src[i]) ++mismatches;
    }
    readback.Unmap();

    CHECK_EQ(mismatches, 0);
}

TEST(UploadReadback, PartialRangeRoundTrip) {
    REQUIRE_CORE(core);
    ID3D12Device* device = core->GetDevice();

    const UINT count = 128;
    const UINT64 bytes = static_cast<UINT64>(count) * sizeof(uint32_t);
    std::vector<uint32_t> src(count);
    for (UINT i = 0; i < count; ++i) src[i] = 1000u + i * 11u;

    D3D12UploadBuffer upload;
    upload.Initialize(device, bytes);
    std::memcpy(upload.Map(), src.data(), static_cast<size_t>(bytes));

    D3D12Resource dst = CreateBuffer(
        *core, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12ReadbackBuffer readback;
    readback.Initialize(device, bytes);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    ctx.GetCommandList()->CopyBufferRegion(dst.Get(), 0, upload.Get(), 0, bytes);
    ctx.ResourceBarrier(MakeTransitionBarrier(
        dst.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE));
    ctx.GetCommandList()->CopyBufferRegion(readback.Get(), 0, dst.Get(), 0, bytes);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

    const UINT first = 17;
    const UINT rangeCount = 53;
    const UINT64 offset = static_cast<UINT64>(first) * sizeof(uint32_t);
    const UINT64 rangeBytes = static_cast<UINT64>(rangeCount) * sizeof(uint32_t);

    auto mapped = readback.MapRead(offset, rangeBytes);
    CHECK(mapped);
    CHECK_EQ(mapped.Offset(), offset);
    CHECK_EQ(mapped.Size(), rangeBytes);

    const auto* got = reinterpret_cast<const uint32_t*>(mapped.Data());
    int mismatches = 0;
    for (UINT i = 0; i < rangeCount; ++i) {
        if (got[i] != src[first + i]) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

TEST(UploadReadback, PartialRangeBoundsAndEmptyRange) {
    REQUIRE_CORE(core);

    D3D12ReadbackBuffer readback;
    readback.Initialize(core->GetDevice(), 64);

    auto emptyAtEnd = readback.MapRead(64, 0);
    CHECK(!emptyAtEnd);
    CHECK(emptyAtEnd.Data() == nullptr);
    CHECK_EQ(emptyAtEnd.Offset(), 64ull);
    CHECK_EQ(emptyAtEnd.Size(), 0ull);

    CHECK_THROWS(readback.MapRead(65, 0));
    CHECK_THROWS(readback.MapRead(63, 2));
    CHECK_THROWS(readback.MapRead(1, 64));
}

TEST(UploadReadback, PartialRangePreventsDoubleMap) {
    REQUIRE_CORE(core);

    D3D12ReadbackBuffer readback;
    readback.Initialize(core->GetDevice(), 64);

    auto mapped = readback.MapRead(8, 16);
    CHECK(mapped);
    CHECK_THROWS(readback.MapRead(0, 8));
    CHECK_THROWS(readback.Map());
    CHECK_THROWS(readback.Unmap());

    D3D12MappedReadRange moved = std::move(mapped);
    CHECK(!mapped);
    CHECK(moved);

    moved = D3D12MappedReadRange{};

    // RAII range の破棄後は既存 Map / Unmap API を再び利用できる。
    CHECK(readback.Map() != nullptr);
    readback.Unmap();
}

TEST(UploadReadback, Sizes) {
    REQUIRE_CORE(core);
    D3D12UploadBuffer up;
    up.Initialize(core->GetDevice(), 4096);
    CHECK_EQ(up.GetSizeBytes(), 4096ull);
    CHECK(up.Map() != nullptr);

    D3D12ReadbackBuffer rb;
    rb.Initialize(core->GetDevice(), 2048);
    CHECK_EQ(rb.GetSizeBytes(), 2048ull);
}
