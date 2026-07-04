//
// test_ComputePipeline.cpp - テンプレ Root Signature の Compute を end-to-end で検証
//
#include "TestCommon.hpp"

#include <cstdint>

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
    cl->SetComputeRootSignature(pipeline.GetRootSignature());
    cl->SetPipelineState(pipeline.GetPipelineState());
    cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
    UINT consts[3] = { value, size, size };
    cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 3, consts, 0);
    cl->Dispatch((size + 7) / 8, (size + 7) / 8, 1);

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
