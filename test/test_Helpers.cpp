//
// test_Helpers.cpp - D3D12Helpers の汎用 resource / view / upload / sampler helper
//
#include "TestCommon.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace D3D12CoreLib;

TEST(Helpers, ResourceCreationHelpers) {
    REQUIRE_CORE(core);

    D3D12Resource structured = CreateStructuredBuffer(
        *core, 16, sizeof(float), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON);
    CHECK(structured.Get() != nullptr);
    CHECK_EQ(structured.GetDesc().Width, static_cast<UINT64>(16 * sizeof(float)));
    CHECK((structured.GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);

    D3D12Resource cb = CreateConstantBuffer(*core, 129);
    CHECK(cb.Get() != nullptr);
    CHECK_EQ(cb.GetDesc().Width, static_cast<UINT64>(256));
    CHECK(cb.GetState() == D3D12_RESOURCE_STATE_GENERIC_READ);

    CHECK_THROWS(CreateStructuredBuffer(*core, 0, sizeof(float)));
    CHECK_THROWS(CreateStructuredBuffer(*core, 16, 0));
    CHECK_THROWS(CreateConstantBuffer(*core, 0));
}

TEST(Helpers, SharedTextureCreation) {
    REQUIRE_CORE(core);
    if (!core->DeviceContext().SupportsResourceSharing()) {
        TEST_SKIP("resource sharing is not supported by this adapter");
    }

    D3D12Resource tex = CreateSharedTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CHECK(tex.Get() != nullptr);
    CHECK_EQ(tex.GetWidth(), 16ull);
    CHECK_EQ(tex.GetHeight(), 16u);
}

TEST(Helpers, FullDescSrvAndUav) {
    REQUIRE_CORE(core);

    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, /*shaderVisible*/ true);
    D3D12DescriptorRange range = alloc.AllocateRange(2);

    D3D12Resource srvTex = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    CHECK_NOTHROW(CreateSrv(*core, srvTex.Get(), srvDesc, range.Cpu(0)));

    D3D12Resource uavTex = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    CHECK_NOTHROW(CreateUav(*core, uavTex.Get(), uavDesc, range.Cpu(1)));
    CHECK_THROWS(CreateUav(*core, srvTex.Get(), uavDesc, range.Cpu(1)));
}

TEST(Helpers, ConvenienceBufferViewsAndCbv) {
    REQUIRE_CORE(core);

    D3D12DescriptorAllocator alloc;
    alloc.Initialize(core->GetDevice(),
                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, /*shaderVisible*/ true);
    D3D12DescriptorRange range = alloc.AllocateRange(3);

    D3D12Resource structured = CreateStructuredBuffer(
        *core, 16, sizeof(float), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    CHECK_NOTHROW(CreateBufferSrv(*core, structured, range.Cpu(0), 0, 16, sizeof(float)));
    CHECK_NOTHROW(CreateBufferUav(*core, structured, range.Cpu(1), 0, 16, sizeof(float)));
    CHECK_THROWS(CreateBufferSrv(*core, structured, range.Cpu(0), 0, 16));
    CHECK_THROWS(CreateBufferUav(*core, structured, range.Cpu(1), 0, 16));

    D3D12Resource cb = CreateConstantBuffer(*core, 128);
    CHECK_NOTHROW(CreateConstantBufferView(*core, cb, range.Cpu(2)));
    CHECK_THROWS(CreateConstantBufferView(*core, cb, range.Cpu(2), 1, 256));
}

TEST(Helpers, FullDescRtvAndDsv) {
    REQUIRE_CORE(core);

    D3D12DescriptorAllocator rtvAlloc;
    rtvAlloc.Initialize(core->GetDevice(),
                        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, /*shaderVisible*/ false);
    D3D12DescriptorAllocator dsvAlloc;
    dsvAlloc.Initialize(core->GetDevice(),
                        D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, /*shaderVisible*/ false);

    D3D12Resource rt = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    CHECK_NOTHROW(CreateRtv(*core, rt.Get(), rtvDesc, rtvAlloc.Allocate().cpu));
    CHECK_NOTHROW(CreateTexture2DRtv(*core, rt, rtvAlloc.Allocate().cpu));

    D3D12Resource ds = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_D32_FLOAT,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    CHECK_NOTHROW(CreateDsv(*core, ds.Get(), dsvDesc, dsvAlloc.Allocate().cpu));
    CHECK_NOTHROW(CreateTexture2DDsv(*core, ds, dsvAlloc.Allocate().cpu));
}

TEST(Helpers, TextureSubresourceUpload) {
    REQUIRE_CORE(core);
    ID3D12Device* device = core->GetDevice();

    D3D12Resource tex = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE,
        1, 2);

    const UINT64 uploadSize = GetRequiredUploadSize(*core, tex, 0, 2);
    CHECK(uploadSize > 0);

    std::vector<uint8_t> mip0(16 * 16 * 4, 10);
    std::vector<uint8_t> mip1(8 * 8 * 4, 20);
    D3D12TextureSubresourceData subresources[2] = {};
    subresources[0].data = mip0.data();
    subresources[0].rowPitch = 16 * 4;
    subresources[1].data = mip1.data();
    subresources[1].rowPitch = 8 * 4;

    D3D12UploadBuffer upload;
    upload.Initialize(device, uploadSize);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_NOTHROW(RecordUploadTextureSubresources(
        *core, ctx, tex, upload, subresources, 0, 2,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());
    CHECK(tex.GetState() == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

TEST(Helpers, SingleTextureUploadRejectsMultiSubresource) {
    REQUIRE_CORE(core);

    std::vector<uint8_t> rgba(16 * 16 * 4, 0);

    CHECK_THROWS(CreateTexture2DFromMemory(
        *core, rgba.data(), 16, 16, DXGI_FORMAT_NV12));

    D3D12Resource mipTex = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE,
        1, 2);

    D3D12UploadBuffer upload;
    upload.Initialize(core->GetDevice(), 4096);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_THROWS(RecordUploadTexture2D(
        *core, ctx, mipTex, upload, rgba.data(), 16, 16,
        DXGI_FORMAT_R8G8B8A8_UNORM));
    ctx.Close();
}

TEST(Helpers, SamplerHelpers) {
    REQUIRE_CORE(core);

    D3D12_SAMPLER_DESC linear = MakeLinearClampSamplerDesc();
    CHECK(linear.Filter == D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    CHECK(linear.AddressU == D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CHECK(linear.AddressV == D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    D3D12_SAMPLER_DESC point = MakePointClampSamplerDesc();
    CHECK(point.Filter == D3D12_FILTER_MIN_MAG_MIP_POINT);
    CHECK(point.AddressU == D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    D3D12DescriptorAllocator samplerAlloc;
    samplerAlloc.Initialize(core->GetDevice(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, /*shaderVisible*/ true);
    CHECK_NOTHROW(CreateSampler(*core, linear, samplerAlloc.Allocate().cpu));
}
