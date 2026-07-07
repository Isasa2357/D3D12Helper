#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>

#include <utility>

using namespace D3D12CoreLib;

TEST(Hardening, SharedHandleEdges) {
    CHECK(!IsValidSharedHandle(nullptr));
    CHECK(!IsValidSharedHandle(INVALID_HANDLE_VALUE));
    CHECK_THROWS(DuplicateSharedHandle(nullptr));

    HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    CHECK(eventHandle != nullptr);

    D3D12SharedHandle owned(eventHandle);
    CHECK(owned.IsValid());

    D3D12SharedHandle moved(std::move(owned));
    CHECK(!owned.IsValid());
    CHECK(moved.IsValid());

    auto duplicate = DuplicateSharedHandle(moved.Get());
    CHECK(duplicate.IsValid());

    moved.Reset();
    CHECK(!moved.IsValid());
}

TEST(Hardening, StateTransitionEdges) {
    REQUIRE_CORE(core);

    auto tex = CreateTexture2D(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto ctx = core->CreateDirectContext();
    ctx.Reset();

    CHECK_EQ(RecordTransitions(ctx, static_cast<const D3D12StateTransition*>(nullptr), 0), 0u);
    CHECK_THROWS(RecordTransitions(ctx, static_cast<const D3D12StateTransition*>(nullptr), 1));

    D3D12StateTransition noop{ &tex, D3D12_RESOURCE_STATE_COMMON };
    CHECK_EQ(RecordTransitions(ctx, &noop, 1), 0u);

    D3D12StateTransition bad{ nullptr, D3D12_RESOURCE_STATE_COPY_SOURCE };
    CHECK_THROWS(RecordTransitions(ctx, &bad, 1));

    D3D12Resource empty;
    CHECK_THROWS(MakeTrackedTransitionBarrier(empty, D3D12_RESOURCE_STATE_COPY_SOURCE));

    ctx.Close();
}

TEST(Hardening, ViewDescriptorEdges) {
    REQUIRE_CORE(core);

    auto buffer = CreateBuffer(*core, 128, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    CHECK_THROWS(MakeConstantBufferViewDesc(buffer, 129, 0));
    CHECK_THROWS(MakeConstantBufferViewDesc(buffer, 64, 128));

    auto typedSrv = MakeBufferSrvDesc(buffer, 0, 4, 0, DXGI_FORMAT_R32_FLOAT);
    CHECK_EQ(typedSrv.Format, DXGI_FORMAT_R32_FLOAT);
    CHECK_EQ(typedSrv.Buffer.StructureByteStride, 0u);

    CHECK_THROWS(MakeBufferSrvDesc(buffer, 0, 1, 0, DXGI_FORMAT_UNKNOWN));
    CHECK_THROWS(MakeBufferUavDesc(buffer, 0, 0, 4, DXGI_FORMAT_UNKNOWN));
}

TEST(Hardening, CopyResolveMipmapEdges) {
    REQUIRE_CORE(core);

    auto src = CreateBuffer(*core, 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto dst = CreateBuffer(*core, 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto ctx = core->CreateDirectContext();
    ctx.Reset();

    CHECK_THROWS(RecordCopyBufferRegion(ctx, dst, 0, src, 0, 0));
    CHECK_THROWS(RecordCopyBufferRegion(ctx, dst, 8, src, 0, 16));
    CHECK_THROWS(RecordCopyBufferRegion(ctx, dst, 0, src, 8, 16));

    auto tex = CreateTexture2D(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    CHECK_THROWS(RecordResolveSubresource(ctx, tex, 0, tex, 0));

    CHECK_EQ(CalculateMipLevelCount(0, 4), 0u);
    CHECK_THROWS(GetMipLevelInfo(0, 4, 0));
    CHECK_THROWS(CreateMipmappedTexture2D(*core, 0, 4, DXGI_FORMAT_R8G8B8A8_UNORM));

    ctx.Close();
}
