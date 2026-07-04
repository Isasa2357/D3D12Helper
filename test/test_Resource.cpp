//
// test_Resource.cpp - リソース生成ヘルパと D3D12Resource の状態追跡
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(Resource, CreateBuffer) {
    REQUIRE_CORE(core);
    D3D12Resource buf = CreateBuffer(
        *core, 1024, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

    CHECK(static_cast<bool>(buf));
    CHECK(buf.Get() != nullptr);
    CHECK(buf.GetState() == D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_RESOURCE_DESC d = buf.GetDesc();
    CHECK(d.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    CHECK_EQ(d.Width, 1024ull);
}

TEST(Resource, CreateTexture2D) {
    REQUIRE_CORE(core);
    D3D12Resource tex = CreateTexture2D(
        *core, 128, 64, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    CHECK(tex.Get() != nullptr);
    CHECK_EQ(tex.GetWidth(), 128ull);
    CHECK_EQ(tex.GetHeight(), 64u);
    CHECK(tex.GetFormat() == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(tex.GetState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

TEST(Resource, StateTracking) {
    REQUIRE_CORE(core);
    D3D12Resource tex = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COPY_DEST);
    tex.SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CHECK(tex.GetState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

TEST(Resource, UavRequiresFlag) {
    REQUIRE_CORE(core);
    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, /*shaderVisible*/ true);

    // UAV フラグ無しのテクスチャに UAV を作ろうとすると例外。
    D3D12Resource noUav = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12DescriptorHandle h0 = alloc.Allocate();
    CHECK_THROWS(CreateTexture2DUav(*core, noUav, h0.cpu));

    // UAV フラグ付きなら成功。
    D3D12Resource withUav = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12DescriptorHandle h1 = alloc.Allocate();
    CHECK_NOTHROW(CreateTexture2DUav(*core, withUav, h1.cpu));
}
