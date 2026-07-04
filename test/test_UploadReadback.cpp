//
// test_UploadReadback.cpp - UploadBuffer -> GPU -> ReadbackBuffer の往復
//
#include "TestCommon.hpp"

#include <cstdint>
#include <cstring>
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

    const auto* got = static_cast<const uint32_t*>(readback.Map());
    int mismatches = 0;
    for (UINT i = 0; i < count; ++i) {
        if (got[i] != src[i]) ++mismatches;
    }
    readback.Unmap();

    CHECK_EQ(mismatches, 0);
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
