//
// test_Helpers.cpp - D3D12Helpers の汎用 view / sampler helper
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

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

TEST(Helpers, FullDescRtvAndDsv) {
    REQUIRE_CORE(core);

    D3D12DescriptorAllocator rtvAlloc;
    rtvAlloc.Initialize(core->GetDevice(),
                        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, /*shaderVisible*/ false);
    D3D12DescriptorAllocator dsvAlloc;
    dsvAlloc.Initialize(core->GetDevice(),
                        D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, /*shaderVisible*/ false);

    D3D12Resource rt = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    CHECK_NOTHROW(CreateRtv(*core, rt.Get(), rtvDesc, rtvAlloc.Allocate().cpu));

    D3D12Resource ds = CreateTexture2D(
        *core, 16, 16, DXGI_FORMAT_D32_FLOAT,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    CHECK_NOTHROW(CreateDsv(*core, ds.Get(), dsvDesc, dsvAlloc.Allocate().cpu));
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
