#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cstdint>

using namespace D3D12CoreLib;

TEST(Binding, Validation) {
    D3D12BindingSet set;
    CHECK(set.Empty());

    uint32_t values[4] = { 1, 2, 3, 4 };
    CHECK_THROWS(set.AddDescriptorTable(UINT_MAX, D3D12_GPU_DESCRIPTOR_HANDLE{ 1 }));
    CHECK_THROWS(set.AddDescriptorTable(0, D3D12_GPU_DESCRIPTOR_HANDLE{}));
    CHECK_THROWS(set.Add32BitConstants(0, nullptr, 4));
    CHECK_THROWS(set.Add32BitConstants(0, values, 0));
    CHECK_THROWS(set.AddConstantBufferView(0, 0));
    CHECK_THROWS(set.BindCompute(nullptr));

    set.Add32BitConstants(0, values, 4);
    CHECK_EQ(set.BindingCount(), static_cast<size_t>(1));
    set.Clear();
    CHECK(set.Empty());
}

TEST(Binding, DescriptorHeapSet) {
    REQUIRE_CORE(core);

    D3D12DescriptorAllocator cbvSrvUav;
    cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    D3D12DescriptorAllocator sampler;
    sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);

    D3D12DescriptorHeapSet heaps;
    heaps.cbvSrvUavHeap = cbvSrvUav.GetHeap();
    heaps.samplerHeap = sampler.GetHeap();
    CHECK_EQ(heaps.Count(), 2u);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    heaps.Bind(ctx.GetCommandList());
    ctx.Close();
}

TEST(Binding, ComputeBindingSet) {
    REQUIRE_CORE(core);

    const char* hlsl =
        "[numthreads(1,1,1)]\n"
        "void main(uint3 tid : SV_DispatchThreadID) { }\n";
    auto cs = CompileShaderFromSource_D3DCompile(hlsl, "main", "cs_5_1");

    D3D12ComputePipeline pipe;
    ComputePipelineDesc desc;
    desc.numSrvs = 1;
    desc.numRootConstantValues = 4;
    pipe.InitializeWithTemplate(core->GetDevice(), cs, desc);

    D3D12DescriptorAllocator srvHeap;
    srvHeap.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto srvHandle = srvHeap.Allocate();

    uint32_t constants[4] = { 10, 20, 30, 40 };
    D3D12BindingSet set;
    set.SetDescriptorHeaps(D3D12DescriptorHeapSet{ srvHeap.GetHeap(), nullptr });
    set.AddDescriptorTable(pipe.SrvTableIndex(), srvHandle);
    set.Add32BitConstants(pipe.RootConstantsIndex(), constants, 4);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    pipe.Bind(ctx);
    set.BindCompute(ctx.GetCommandList());
    pipe.Dispatch(ctx, 1, 1, 1);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();
}
