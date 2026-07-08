#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <cstdint>
#include <stdexcept>
#include <utility>

using namespace D3D12CoreLib;

namespace {

ComPtr<ID3D12RootSignature> MakeMixedRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE1 tableRange = {};
    tableRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tableRange.NumDescriptors = 1;
    tableRange.BaseShaderRegister = 0;
    tableRange.RegisterSpace = 0;
    tableRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    tableRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 params[5] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &tableRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[3].Descriptor.ShaderRegister = 1;
    params[3].Descriptor.RegisterSpace = 0;
    params[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[4].Descriptor.ShaderRegister = 0;
    params[4].Descriptor.RegisterSpace = 0;
    params[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = static_cast<UINT>(_countof(params));
    desc.Desc_1_1.pParameters = params;
    desc.Desc_1_1.NumStaticSamplers = 0;
    desc.Desc_1_1.pStaticSamplers = nullptr;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    D3D12CORE_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&desc, &blob, &error));

    ComPtr<ID3D12RootSignature> rootSig;
    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    return rootSig;
}

D3D12BindingSet MakeMixedBindingSet(
    D3D12DescriptorHandle tableHandle,
    D3D12_GPU_VIRTUAL_ADDRESS cbvAddress,
    D3D12_GPU_VIRTUAL_ADDRESS srvAddress,
    D3D12_GPU_VIRTUAL_ADDRESS uavAddress,
    ID3D12DescriptorHeap* heap) {

    uint32_t constants[4] = { 10, 20, 30, 40 };
    D3D12BindingSet set;
    set.SetDescriptorHeaps(D3D12DescriptorHeapSet{ heap, nullptr });
    set.AddDescriptorTable(0, tableHandle);
    set.Add32BitConstants(1, constants, 4);
    set.AddConstantBufferView(2, cbvAddress);
    set.AddShaderResourceView(3, srvAddress);
    set.AddUnorderedAccessView(4, uavAddress);
    return set;
}

} // namespace

TEST(Binding, Validation) {
    D3D12BindingSet set;
    CHECK(set.Empty());
    CHECK_EQ(set.BindingCount(), static_cast<size_t>(0));
    CHECK(set.DescriptorHeaps().Empty());

    uint32_t values[4] = { 1, 2, 3, 4 };
    D3D12DescriptorHandle nonVisibleHandle;
    nonVisibleHandle.cpu.ptr = 1;
    nonVisibleHandle.gpu.ptr = 1;
    nonVisibleHandle.shaderVisible = false;

    D3D12DescriptorHandle noGpuHandle;
    noGpuHandle.cpu.ptr = 1;
    noGpuHandle.shaderVisible = true;

    CHECK_THROWS(set.AddDescriptorTable(UINT_MAX, D3D12_GPU_DESCRIPTOR_HANDLE{ 1 }));
    CHECK_THROWS(set.AddDescriptorTable(0, D3D12_GPU_DESCRIPTOR_HANDLE{}));
    CHECK_THROWS(set.AddDescriptorTable(0, nonVisibleHandle));
    CHECK_THROWS(set.AddDescriptorTable(0, noGpuHandle));
    CHECK_THROWS(set.Add32BitConstants(UINT_MAX, values, 4));
    CHECK_THROWS(set.Add32BitConstants(0, nullptr, 4));
    CHECK_THROWS(set.Add32BitConstants(0, values, 0));
    CHECK_THROWS(set.AddConstantBufferView(UINT_MAX, 256));
    CHECK_THROWS(set.AddConstantBufferView(0, 0));
    CHECK_THROWS(set.AddShaderResourceView(UINT_MAX, 256));
    CHECK_THROWS(set.AddShaderResourceView(0, 0));
    CHECK_THROWS(set.AddUnorderedAccessView(UINT_MAX, 256));
    CHECK_THROWS(set.AddUnorderedAccessView(0, 0));
    CHECK_THROWS(set.BindCompute(nullptr));
    CHECK_THROWS(set.BindGraphics(nullptr));

    set.SetDescriptorHeaps(D3D12DescriptorHeapSet{ reinterpret_cast<ID3D12DescriptorHeap*>(1), nullptr });
    set.Add32BitConstants(0, values, 4, 1);
    CHECK_EQ(set.BindingCount(), static_cast<size_t>(1));
    CHECK(!set.Empty());
    CHECK(!set.DescriptorHeaps().Empty());
    set.Clear();
    CHECK(set.Empty());
    CHECK(set.DescriptorHeaps().Empty());
}

TEST(Binding, DescriptorHeapSet) {
    REQUIRE_CORE(core);

    D3D12DescriptorHeapSet empty;
    CHECK(empty.Empty());
    CHECK_EQ(empty.Count(), 0u);
    CHECK_THROWS(empty.Bind(nullptr));
    CHECK_THROWS(SetDescriptorHeaps(nullptr, empty));

    D3D12DescriptorAllocator cbvSrvUav;
    cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    D3D12DescriptorAllocator sampler;
    sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);

    D3D12DescriptorHeapSet onlyCbvSrvUav;
    onlyCbvSrvUav.cbvSrvUavHeap = cbvSrvUav.GetHeap();
    CHECK(!onlyCbvSrvUav.Empty());
    CHECK_EQ(onlyCbvSrvUav.Count(), 1u);

    D3D12DescriptorHeapSet onlySampler;
    onlySampler.samplerHeap = sampler.GetHeap();
    CHECK(!onlySampler.Empty());
    CHECK_EQ(onlySampler.Count(), 1u);

    D3D12DescriptorHeapSet heaps;
    heaps.cbvSrvUavHeap = cbvSrvUav.GetHeap();
    heaps.samplerHeap = sampler.GetHeap();
    CHECK_EQ(heaps.Count(), 2u);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_NOTHROW(empty.Bind(ctx.GetCommandList()));
    CHECK_NOTHROW(onlyCbvSrvUav.Bind(ctx.GetCommandList()));
    CHECK_NOTHROW(onlySampler.Bind(ctx.GetCommandList()));
    CHECK_NOTHROW(heaps.Bind(ctx.GetCommandList()));
    ctx.Close();
}

TEST(Binding, DirectHelperValidation) {
    REQUIRE_CORE(core);
    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    auto* cl = ctx.GetCommandList();
    D3D12_GPU_DESCRIPTOR_HANDLE validHandle{ 1 };
    uint32_t constants[4] = { 1, 2, 3, 4 };

    CHECK_THROWS(SetComputeDescriptorTable(nullptr, 0, validHandle));
    CHECK_THROWS(SetComputeDescriptorTable(cl, UINT_MAX, validHandle));
    CHECK_THROWS(SetComputeDescriptorTable(cl, 0, D3D12_GPU_DESCRIPTOR_HANDLE{}));
    CHECK_THROWS(SetGraphicsDescriptorTable(nullptr, 0, validHandle));
    CHECK_THROWS(SetGraphicsDescriptorTable(cl, UINT_MAX, validHandle));
    CHECK_THROWS(SetGraphicsDescriptorTable(cl, 0, D3D12_GPU_DESCRIPTOR_HANDLE{}));

    CHECK_THROWS(SetCompute32BitConstants(nullptr, 0, constants, 4));
    CHECK_THROWS(SetCompute32BitConstants(cl, UINT_MAX, constants, 4));
    CHECK_THROWS(SetCompute32BitConstants(cl, 0, nullptr, 4));
    CHECK_THROWS(SetCompute32BitConstants(cl, 0, constants, 0));
    CHECK_THROWS(SetGraphics32BitConstants(nullptr, 0, constants, 4));
    CHECK_THROWS(SetGraphics32BitConstants(cl, UINT_MAX, constants, 4));
    CHECK_THROWS(SetGraphics32BitConstants(cl, 0, nullptr, 4));
    CHECK_THROWS(SetGraphics32BitConstants(cl, 0, constants, 0));
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

TEST(Binding, ComputeBindingSetCoversAllRootBindingTypes) {
    REQUIRE_CORE(core);

    const char* hlsl =
        "[numthreads(1,1,1)]\n"
        "void main(uint3 tid : SV_DispatchThreadID) { }\n";
    auto cs = CompileShaderFromSource_D3DCompile(hlsl, "main", "cs_5_1");
    auto rootSig = MakeMixedRootSignature(core->GetDevice());

    D3D12ComputePipeline pipe;
    pipe.Initialize(core->GetDevice(), rootSig, cs);

    D3D12DescriptorAllocator heap;
    heap.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto tableHandle = heap.Allocate();

    auto cbv = CreateConstantBuffer(*core, 256);
    auto srv = CreateBuffer(*core, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto uav = CreateStructuredBuffer(*core, 16, 4);
    auto set = MakeMixedBindingSet(
        tableHandle,
        cbv.Get()->GetGPUVirtualAddress(),
        srv.Get()->GetGPUVirtualAddress(),
        uav.Get()->GetGPUVirtualAddress(),
        heap.GetHeap());
    CHECK_EQ(set.BindingCount(), static_cast<size_t>(5));

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

TEST(Binding, GraphicsBindingSetCoversAllRootBindingTypes) {
    REQUIRE_CORE(core);

    const char* hlsl = R"(
float4 VSMain(uint vid : SV_VertexID) : SV_POSITION
{
    float2 p[3] = { float2(-1.0f, -1.0f), float2(3.0f, -1.0f), float2(-1.0f, 3.0f) };
    return float4(p[vid], 0.0f, 1.0f);
}
float4 PSMain() : SV_TARGET
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)";
    auto vs = CompileShaderFromSource_D3DCompile(hlsl, "VSMain", "vs_5_1");
    auto ps = CompileShaderFromSource_D3DCompile(hlsl, "PSMain", "ps_5_1");

    GraphicsPipelineDesc gd;
    gd.vs = std::move(vs);
    gd.ps = std::move(ps);
    gd.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
    gd.rasterizer = &noCull;

    auto rootSig = MakeMixedRootSignature(core->GetDevice());
    D3D12GraphicsPipeline pipe;
    pipe.Initialize(core->GetDevice(), rootSig, gd);

    D3D12DescriptorAllocator heap;
    heap.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto tableHandle = heap.Allocate();

    auto cbv = CreateConstantBuffer(*core, 256);
    auto srv = CreateBuffer(*core, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    auto uav = CreateStructuredBuffer(*core, 16, 4);
    auto set = MakeMixedBindingSet(
        tableHandle,
        cbv.Get()->GetGPUVirtualAddress(),
        srv.Get()->GetGPUVirtualAddress(),
        uav.Get()->GetGPUVirtualAddress(),
        heap.GetHeap());

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    pipe.Bind(ctx);
    set.BindGraphics(ctx.GetCommandList());
    ctx.Close();
}

TEST(Binding, DirectHelpersCanBindWithRootSignature) {
    REQUIRE_CORE(core);

    const char* hlsl =
        "[numthreads(1,1,1)]\n"
        "void main(uint3 tid : SV_DispatchThreadID) { }\n";
    auto cs = CompileShaderFromSource_D3DCompile(hlsl, "main", "cs_5_1");
    auto rootSig = MakeMixedRootSignature(core->GetDevice());
    D3D12ComputePipeline pipe;
    pipe.Initialize(core->GetDevice(), rootSig, cs);

    D3D12DescriptorAllocator heap;
    heap.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto tableHandle = heap.Allocate();
    uint32_t constants[4] = { 4, 3, 2, 1 };

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    pipe.Bind(ctx);
    SetDescriptorHeaps(ctx.GetCommandList(), D3D12DescriptorHeapSet{ heap.GetHeap(), nullptr });
    SetComputeDescriptorTable(ctx.GetCommandList(), 0, tableHandle.gpu);
    SetCompute32BitConstants(ctx.GetCommandList(), 1, constants, 4, 0);
    pipe.Dispatch(ctx, 1, 1, 1);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();
}
