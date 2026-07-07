#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <string>
#include <vector>

using namespace D3D12CoreLib;

TEST(ViewState, ViewDescriptorHelpers) {
    REQUIRE_CORE(core);

    auto tex = CreateMipmappedTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto srv = MakeTexture2DSrvDesc(tex);
    CHECK_EQ(srv.ViewDimension, D3D12_SRV_DIMENSION_TEXTURE2D);
    CHECK_EQ(srv.Texture2D.MostDetailedMip, 0u);
    CHECK_EQ(srv.Texture2D.MipLevels, 3u);

    auto uav = MakeTexture2DUavDesc(tex, DXGI_FORMAT_UNKNOWN, 1);
    CHECK_EQ(uav.ViewDimension, D3D12_UAV_DIMENSION_TEXTURE2D);
    CHECK_EQ(uav.Texture2D.MipSlice, 1u);

    auto rtv = MakeTexture2DRtvDesc(tex, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    CHECK_EQ(rtv.ViewDimension, D3D12_RTV_DIMENSION_TEXTURE2D);
    CHECK_EQ(rtv.Texture2D.MipSlice, 2u);

    auto buf = CreateStructuredBuffer(*core, 8, 4);
    auto bsrv = MakeBufferSrvDesc(buf, 0, 8, 4);
    CHECK_EQ(bsrv.ViewDimension, D3D12_SRV_DIMENSION_BUFFER);
    CHECK_EQ(bsrv.Buffer.NumElements, 8u);
    CHECK_EQ(bsrv.Buffer.StructureByteStride, 4u);

    auto cb = CreateConstantBuffer(*core, 512);
    auto cbv = MakeConstantBufferViewDesc(cb, 0, 256);
    CHECK_EQ(cbv.SizeInBytes, 256u);
    CHECK(cbv.BufferLocation != 0);

    CHECK_THROWS(MakeTexture2DSrvDesc(buf));
    CHECK_THROWS(MakeTexture2DUavDesc(tex, DXGI_FORMAT_UNKNOWN, 99));
    CHECK_THROWS(MakeBufferSrvDesc(buf, 0, 0, 4));
}

TEST(ViewState, DescriptorHandleHelpers) {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    CHECK(!IsCpuDescriptorValid(cpu));
    CHECK(!IsGpuDescriptorValid(gpu));
    cpu.ptr = 123;
    gpu.ptr = 456;
    CHECK(IsCpuDescriptorValid(cpu));
    CHECK(IsGpuDescriptorValid(gpu));

    D3D12DescriptorHandle handle;
    handle.cpu = cpu;
    handle.gpu = gpu;
    handle.shaderVisible = true;
    CHECK(IsShaderVisibleDescriptor(handle));
    handle.shaderVisible = false;
    CHECK(!IsShaderVisibleDescriptor(handle));
}

TEST(ViewState, StateClassification) {
    const auto shaderRead = static_cast<D3D12_RESOURCE_STATES>(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CHECK(HasResourceState(shaderRead, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    CHECK(IsReadOnlyResourceState(D3D12_RESOURCE_STATE_COPY_SOURCE));
    CHECK(IsWriteResourceState(D3D12_RESOURCE_STATE_COPY_DEST));
    CHECK(CanImplicitlyPromoteTo(D3D12_RESOURCE_STATE_COPY_SOURCE));
    CHECK(!CanImplicitlyPromoteTo(D3D12_RESOURCE_STATE_RENDER_TARGET));
    CHECK(std::string(ResourceStateName(D3D12_RESOURCE_STATE_COMMON)) == "COMMON/PRESENT");
}

TEST(ViewState, TrackedTransitions) {
    REQUIRE_CORE(core);

    auto tex = CreateTexture2D(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    auto buf = CreateBuffer(*core, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

    auto barrier = MakeTrackedTransitionBarrier(tex, D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK_EQ(barrier.Transition.StateBefore, D3D12_RESOURCE_STATE_COMMON);
    CHECK_EQ(barrier.Transition.StateAfter, D3D12_RESOURCE_STATE_COPY_DEST);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK(RecordTransition(ctx, tex, D3D12_RESOURCE_STATE_COPY_DEST));
    CHECK_EQ(tex.GetState(), D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(!RecordTransition(ctx, tex, D3D12_RESOURCE_STATE_COPY_DEST));

    std::vector<D3D12StateTransition> transitions;
    transitions.push_back(D3D12StateTransition{ &tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE });
    transitions.push_back(D3D12StateTransition{ &buf, D3D12_RESOURCE_STATE_COPY_SOURCE });
    CHECK_EQ(RecordTransitions(ctx, transitions), 2u);
    CHECK_EQ(tex.GetState(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    CHECK_EQ(buf.GetState(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    ctx.Close();
}
