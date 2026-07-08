//
// test_ComputePipeline.cpp - テンプレ Root Signature の Compute を end-to-end で検証
//
#include "TestCommon.hpp"

#include <cstdint>
#include <stdexcept>

using namespace D3D12CoreLib;

namespace {
// u0 を gValue/255 で塗る。
const char* kFillCs = R"(
RWTexture2D<float4> gOutput : register(u0);
cbuffer C : register(b0) { uint gValue; uint gWidth; uint gHeight; }
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gWidth || id.y >= gHeight) return;
    float v = gValue / 255.0f;
    gOutput[id.xy] = float4(v, v, v, 1.0f);
}
)";

const char* kNoResourceCs = R"(
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) { }
)";

ComPtr<ID3D12RootSignature> MakeComputeRootSig(ID3D12Device* device) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC v = {};
    v.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    v.Desc_1_1.NumParameters = 0;
    v.Desc_1_1.pParameters = nullptr;
    v.Desc_1_1.NumStaticSamplers = 0;
    v.Desc_1_1.pStaticSamplers = nullptr;
    v.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> err;
    D3D12CORE_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&v, &blob, &err));

    ComPtr<ID3D12RootSignature> rootSig;
    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    return rootSig;
}

} // namespace

TEST(ComputePipeline, TemplateSlotIndices) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ShaderBytecode cs = CompileShaderFromSource_Dxc(kFillCs, "main", "cs_6_0");

    ComputePipelineDesc pd;
    pd.numUavs               = 1;
    pd.numRootConstantValues = 3;
    D3D12ComputePipeline pipeline;
    pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);

    CHECK(pipeline.GetRootSignature() != nullptr);
    CHECK(pipeline.GetPipelineState() != nullptr);
    // SRV は無いので UINT_MAX、UAV と Root 定数は有効なインデックス。
    CHECK(pipeline.SrvTableIndex() == UINT_MAX);
    CHECK(pipeline.UavTableIndex() != UINT_MAX);
    CHECK(pipeline.RootConstantsIndex() != UINT_MAX);
}

TEST(ComputePipeline, TemplateSlotIndexCombinations) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ShaderBytecode cs = CompileShaderFromSource_Dxc(kNoResourceCs, "main", "cs_6_0");

    {
        ComputePipelineDesc pd;
        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);
        CHECK(pipeline.GetRootSignature() != nullptr);
        CHECK(pipeline.GetPipelineState() != nullptr);
        CHECK(pipeline.SrvTableIndex() == UINT_MAX);
        CHECK(pipeline.UavTableIndex() == UINT_MAX);
        CHECK(pipeline.RootConstantsIndex() == UINT_MAX);
    }

    {
        ComputePipelineDesc pd;
        pd.numSrvs = 2;
        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);
        CHECK_EQ(pipeline.SrvTableIndex(), 0u);
        CHECK(pipeline.UavTableIndex() == UINT_MAX);
        CHECK(pipeline.RootConstantsIndex() == UINT_MAX);
    }

    {
        ComputePipelineDesc pd;
        pd.numUavs = 2;
        pd.numRootConstantValues = 4;
        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);
        CHECK(pipeline.SrvTableIndex() == UINT_MAX);
        CHECK_EQ(pipeline.UavTableIndex(), 0u);
        CHECK_EQ(pipeline.RootConstantsIndex(), 1u);
    }

    {
        ComputePipelineDesc pd;
        pd.numSrvs = 1;
        pd.numUavs = 1;
        pd.numRootConstantValues = 1;
        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);
        CHECK_EQ(pipeline.SrvTableIndex(), 0u);
        CHECK_EQ(pipeline.UavTableIndex(), 1u);
        CHECK_EQ(pipeline.RootConstantsIndex(), 2u);
    }
}

TEST(ComputePipeline, InitializeRejectsInvalidArguments) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ShaderBytecode cs = CompileShaderFromSource_Dxc(kNoResourceCs, "main", "cs_6_0");
    ShaderBytecode empty;
    D3D12ComputePipeline pipeline;
    ComputePipelineDesc pd;

    CHECK_THROWS(pipeline.InitializeWithTemplate(nullptr, cs, pd));
    CHECK_THROWS(pipeline.InitializeWithTemplate(core->GetDevice(), empty, pd));
    CHECK_THROWS(pipeline.Initialize(nullptr, MakeComputeRootSig(core->GetDevice()), cs));
    CHECK_THROWS(pipeline.Initialize(core->GetDevice(), nullptr, cs));
    CHECK_THROWS(pipeline.Initialize(core->GetDevice(), MakeComputeRootSig(core->GetDevice()), empty));
}

TEST(ComputePipeline, CustomRootSignatureInitializeAndReinitialize) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ShaderBytecode cs = CompileShaderFromSource_Dxc(kNoResourceCs, "main", "cs_6_0");

    D3D12ComputePipeline pipeline;
    pipeline.Initialize(core->GetDevice(), MakeComputeRootSig(core->GetDevice()), cs);
    CHECK(pipeline.GetRootSignature() != nullptr);
    CHECK(pipeline.GetPipelineState() != nullptr);
    CHECK(pipeline.SrvTableIndex() == UINT_MAX);
    CHECK(pipeline.UavTableIndex() == UINT_MAX);
    CHECK(pipeline.RootConstantsIndex() == UINT_MAX);

    ComputePipelineDesc pd;
    pd.numSrvs = 1;
    pd.numUavs = 1;
    pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);
    CHECK_EQ(pipeline.SrvTableIndex(), 0u);
    CHECK_EQ(pipeline.UavTableIndex(), 1u);
    CHECK(pipeline.RootConstantsIndex() == UINT_MAX);
}

TEST(ComputePipeline, BindRejectsUninitializedPipeline) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    D3D12ComputePipeline pipeline;
    CHECK_THROWS(pipeline.Bind(ctx));
    CHECK_THROWS(pipeline.Bind(static_cast<ID3D12GraphicsCommandList*>(nullptr)));
    CHECK_THROWS(pipeline.Dispatch(ctx, 1, 1, 1));
    CHECK_THROWS(pipeline.Dispatch(static_cast<ID3D12GraphicsCommandList*>(nullptr), 1, 1, 1));
    ctx.Close();
}

TEST(ComputePipeline, DispatchRejectsZeroGroupCounts) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ShaderBytecode cs = CompileShaderFromSource_Dxc(kNoResourceCs, "main", "cs_6_0");

    D3D12ComputePipeline pipeline;
    pipeline.Initialize(core->GetDevice(), MakeComputeRootSig(core->GetDevice()), cs);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    pipeline.Bind(ctx);
    CHECK_THROWS(pipeline.Dispatch(ctx, 0, 1, 1));
    CHECK_THROWS(pipeline.Dispatch(ctx, 1, 0, 1));
    CHECK_THROWS(pipeline.Dispatch(ctx, 1, 1, 0));
    ctx.Close();
}

TEST(ComputePipeline, DispatchFillAndReadback) {
    REQUIRE_CORE(core);
    REQUIRE_DXC();
    ID3D12Device* device = core->GetDevice();

    const UINT size  = 32;
    const UINT value = 200;

    ShaderBytecode cs = CompileShaderFromSource_Dxc(kFillCs, "main", "cs_6_0");
    ComputePipelineDesc pd;
    pd.numUavs               = 1;
    pd.numRootConstantValues = 3;
    D3D12ComputePipeline pipeline;
    pipeline.InitializeWithTemplate(device, cs, pd);

    D3D12Resource output = CreateTexture2D(
        *core, size, size, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12DescriptorAllocator alloc;
    alloc.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
    D3D12DescriptorHandle uav = alloc.Allocate();
    CreateTexture2DUav(*core, output, uav.cpu);

    // リードバックのフットプリント
    D3D12_RESOURCE_DESC od = output.GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT   rows = 0; UINT64 rowSize = 0, total = 0;
    device->GetCopyableFootprints(&od, 0, 1, 0, &fp, &rows, &rowSize, &total);

    D3D12ReadbackBuffer readback;
    readback.Initialize(device, total);

    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    auto* cl = ctx.GetCommandList();

    ID3D12DescriptorHeap* heaps[] = { alloc.GetHeap() };
    cl->SetDescriptorHeaps(1, heaps);
    pipeline.Bind(ctx);
    cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
    UINT consts[3] = { value, size, size };
    cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 3, consts, 0);
    pipeline.Dispatch(ctx, (size + 7) / 8, (size + 7) / 8, 1);

    ctx.ResourceBarrier(MakeTransitionBarrier(
        output.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE));

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = output.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

    const auto* base = static_cast<const uint8_t*>(readback.Map());
    const uint8_t* center = base + (size / 2) * fp.Footprint.RowPitch + (size / 2) * 4;
    int diff = (int)center[0] - (int)value;
    if (diff < 0) diff = -diff;
    readback.Unmap();

    CHECK(diff <= 1);   // 丸め誤差 ±1 は許容
}
