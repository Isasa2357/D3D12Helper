//
// 05_BufferCompute / main.cpp
//
// GPU バッファに対する Compute（SAXPY: y = a*x + y）。
// これまでのテクスチャ系サンプルが触れていない次を網羅する:
//   - CreateBuffer（DEFAULT ヒープのバッファ）
//   - D3D12UploadBuffer + CopyBufferRegion による CPU -> GPU 転送
//   - 自前 Root Signature を作って ComputePipeline::Initialize（テンプレ以外の経路）
//   - Root Descriptor（ディスクリプタヒープ不要で SRV/UAV をバインド）
//   - バッファのリードバック検証
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <vector>

using namespace D3D12CoreLib;

namespace {

constexpr UINT  kCount = 1024;
constexpr float kA     = 2.0f;

// StructuredBuffer<float> x (t0) と RWStructuredBuffer<float> y (u0)。
// Root 定数 b0 に a と要素数を渡す。
const char* kSaxpyHlsl = R"(
StructuredBuffer<float>   gX : register(t0);
RWStructuredBuffer<float> gY : register(u0);
cbuffer Constants : register(b0) { float gA; uint gCount; }

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= gCount) return;
    gY[i] = gA * gX[i] + gY[i];
}
)";

// SRV(t0) + UAV(u0) を Root Descriptor、b0 を Root 定数にした Root Signature を作る。
ComPtr<ID3D12RootSignature> CreateSaxpyRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER1 params[3] = {};

    // t0: SRV root descriptor
    params[0].ParameterType       = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // u0: UAV root descriptor
    params[1].ParameterType       = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // b0: 32bit 定数 2 個（float a, uint count）
    params[2].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.RegisterSpace  = 0;
    params[2].Constants.Num32BitValues = 2;
    params[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc = {};
    vdesc.Version               = D3D_ROOT_SIGNATURE_VERSION_1_1;
    vdesc.Desc_1_1.NumParameters = 3;
    vdesc.Desc_1_1.pParameters   = params;
    vdesc.Desc_1_1.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&vdesc, &blob, &errors);
    if (FAILED(hr)) {
        std::string msg = "root signature serialize failed";
        if (errors) msg.append(static_cast<const char*>(errors->GetBufferPointer()),
                               errors->GetBufferSize());
        throw std::runtime_error(msg);
    }

    ComPtr<ID3D12RootSignature> rootSig;
    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    return rootSig;
}

} // namespace

int main() {
    try {
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        config.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(config);
        ID3D12Device* device = core->GetDevice();

        std::wcout << L"Adapter: " << core->DeviceContext().GetAdapterName() << L"\n";

        const UINT64 byteSize = static_cast<UINT64>(kCount) * sizeof(float);

        // ---- 入力データ（x[i] = i, y[i] = 1000 + i）----
        std::vector<float> hostX(kCount), hostY(kCount);
        for (UINT i = 0; i < kCount; ++i) {
            hostX[i] = static_cast<float>(i);
            hostY[i] = 1000.0f + static_cast<float>(i);
        }

        // ---- GPU バッファ（DEFAULT ヒープ）----
        D3D12Resource xBuf = CreateBuffer(*core, byteSize, D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12Resource yBuf = CreateBuffer(*core, byteSize, D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        // ---- アップロード用（x と y を連結して 1 つに格納）----
        D3D12UploadBuffer upload;
        upload.Initialize(device, byteSize * 2);
        {
            auto* p = static_cast<uint8_t*>(upload.Map());
            std::memcpy(p,                          hostX.data(), byteSize);
            std::memcpy(p + byteSize,               hostY.data(), byteSize);
        }

        // ---- リードバック用 ----
        D3D12ReadbackBuffer readback;
        readback.Initialize(device, byteSize);

        // ---- パイプライン（自前 Root Signature）----
        ShaderBytecode cs = CompileShaderFromSource_Dxc(kSaxpyHlsl, "main", "cs_6_0");
        ComPtr<ID3D12RootSignature> rootSig = CreateSaxpyRootSignature(device);
        D3D12ComputePipeline pipeline;
        pipeline.Initialize(device, rootSig, cs);   // テンプレートではない経路

        // ---- コマンド記録 ----
        D3D12CommandContext ctx = core->CreateDirectContext();
        ctx.Reset();
        auto* cl = ctx.GetCommandList();

        // upload -> x, y へコピー
        cl->CopyBufferRegion(xBuf.Get(), 0, upload.Get(), 0,        byteSize);
        cl->CopyBufferRegion(yBuf.Get(), 0, upload.Get(), byteSize, byteSize);

        // COPY_DEST -> 読み/書き状態
        D3D12_RESOURCE_BARRIER toRead[2] = {
            MakeTransitionBarrier(xBuf.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            MakeTransitionBarrier(yBuf.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        ctx.ResourceBarrier(2, toRead);

        // バインド（Root Descriptor なのでディスクリプタヒープ不要）
        cl->SetComputeRootSignature(pipeline.GetRootSignature());
        cl->SetPipelineState(pipeline.GetPipelineState());
        cl->SetComputeRootShaderResourceView(0, xBuf.Get()->GetGPUVirtualAddress());
        cl->SetComputeRootUnorderedAccessView(1, yBuf.Get()->GetGPUVirtualAddress());

        UINT consts[2];
        std::memcpy(&consts[0], &kA, sizeof(float));   // float を 32bit 定数として渡す
        consts[1] = kCount;
        cl->SetComputeRoot32BitConstants(2, 2, consts, 0);

        cl->Dispatch((kCount + 63) / 64, 1, 1);

        // y を読み戻す
        ctx.ResourceBarrier(MakeTransitionBarrier(yBuf.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        cl->CopyBufferRegion(readback.Get(), 0, yBuf.Get(), 0, byteSize);

        ctx.Close();

        // ---- 実行・待機 ----
        ID3D12CommandList* lists[] = { ctx.GetCommandList() };
        core->DirectQueue().ExecuteCommandLists(1, lists);
        core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

        // ---- 検証 ----
        const float* result = static_cast<const float*>(readback.Map());
        int   mismatches = 0;
        float maxErr     = 0.0f;
        for (UINT i = 0; i < kCount; ++i) {
            float expected = kA * hostX[i] + hostY[i];
            float err      = std::abs(result[i] - expected);
            if (err > maxErr) maxErr = err;
            if (err > 1e-3f) ++mismatches;
        }
        readback.Unmap();

        std::cout << "SAXPY: y = " << kA << " * x + y  over " << kCount << " elements\n";
        std::cout << "  y[0]   = " << (kA * hostX[0] + hostY[0]) << " (expected)\n";
        std::cout << "  max abs error = " << maxErr << "\n";
        std::cout << (mismatches == 0
                      ? "RESULT: OK - GPU SAXPY matches CPU reference.\n"
                      : "RESULT: FAILED.\n");

        core->WaitIdle();
        return mismatches == 0 ? 0 : 2;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
