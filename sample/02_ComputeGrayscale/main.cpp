//
// 02_ComputeGrayscale / main.cpp
//
// D3D12Helper を一通り使う Compute サンプル。ウィンドウ不要。
//
//   1. D3D12Core を初期化
//   2. DXC で Compute Shader をランタイムコンパイル（SM 6.0）
//   3. CPU 上で作ったカラー画像を入力テクスチャに同期アップロード
//   4. 出力用 UAV テクスチャを作成
//   5. DescriptorAllocator で SRV / UAV を確保
//   6. テンプレート Root Signature で ComputePipeline を作成
//   7. Dispatch でグレースケール変換
//   8. 出力を ReadbackBuffer に CopyTextureRegion して CPU に戻す
//   9. 結果を検証
//
// 使っている D3D12Helper 機能:
//   D3D12Core / D3D12CommandContext / D3D12Queue / D3D12Fence
//   CompileShaderFromSource_Dxc / ComputePipeline(テンプレ RootSig)
//   CreateTexture2DFromRGBA / CreateTexture2D
//   DescriptorAllocator / CreateTexture2DSrv / CreateTexture2DUav
//   MakeTransitionBarrier / ReadbackBuffer / FormatUtil
//
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Core/D3D12Debug.hpp"     // D3D12Debug::SetDebugName
#include "D3D12Core/D3D12Barrier.hpp"   // MakeTransitionBarrier
#include "D3D12Framework/D3D12Framework.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace D3D12CoreLib;

namespace {

// 8x8 スレッドグループ。Dispatch 数の計算と一致させること。
constexpr UINT kThreadGroupSize = 8;
constexpr UINT kWidth  = 256;
constexpr UINT kHeight = 256;

// 入力 t0 をグレースケール化して出力 u0 へ書く。
// Root 定数 b0 で画像サイズを渡す。
const char* kGrayscaleHlsl = R"(
Texture2D<float4>   gInput  : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Constants : register(b0)
{
    uint gWidth;
    uint gHeight;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    float4 c = gInput.Load(int3(id.xy, 0));
    // Rec.601 輝度
    float  l = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));
    gOutput[id.xy] = float4(l, l, l, c.a);
}
)";

// CPU 側で同じ輝度計算を行い、GPU の結果と突き合わせる。
uint8_t ExpectedLuma(uint8_t r, uint8_t g, uint8_t b) {
    float l = 0.299f * r + 0.587f * g + 0.114f * b;
    int   v = static_cast<int>(l + 0.5f);
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return static_cast<uint8_t>(v);
}

// テスト用に色付きのグラデーション画像を作る（RGBA8）。
std::vector<uint8_t> MakeTestImage(UINT w, UINT h) {
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (UINT y = 0; y < h; ++y) {
        for (UINT x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            img[i + 0] = static_cast<uint8_t>(x);              // R: 横グラデ
            img[i + 1] = static_cast<uint8_t>(y);              // G: 縦グラデ
            img[i + 2] = static_cast<uint8_t>((x + y) >> 1);   // B
            img[i + 3] = 255;                                   // A
        }
    }
    return img;
}

} // namespace

int main() {
    try {
        // ---- 1. Core 初期化 ----
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        config.allowWarpAdapter = true;   // CI / GPU 無し環境でも WARP で動く
        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: " << core->DeviceContext().GetAdapterName() << L"\n";

        // ---- 2. シェーダをコンパイル（DXC / SM 6.0）----
        ShaderBytecode cs = CompileShaderFromSource_Dxc(
            kGrayscaleHlsl, "main", "cs_6_0", "Grayscale.hlsl");

        // ---- 3. 入力テクスチャ（同期アップロード）----
        std::vector<uint8_t> srcImage = MakeTestImage(kWidth, kHeight);

        // compute の SRV として読むので NON_PIXEL_SHADER_RESOURCE で受け取る。
        D3D12Resource input = CreateTexture2DFromRGBA(
            *core, srcImage.data(), kWidth, kHeight,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D12Debug::SetDebugName(input.Get(), L"InputTexture");

        // ---- 4. 出力テクスチャ（UAV）----
        D3D12Resource output = CreateTexture2D(
            *core, kWidth, kHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);   // UAV にはこのフラグが必須
        D3D12Debug::SetDebugName(output.Get(), L"OutputTexture");

        // ---- 5. SRV / UAV を確保（shader-visible なヒープ）----
        D3D12DescriptorAllocator alloc;
        alloc.Initialize(core->GetDevice(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         /*count*/ 8, /*shaderVisible*/ true);

        D3D12DescriptorHandle srv = alloc.Allocate();
        CreateTexture2DSrv(*core, input, srv.cpu);

        D3D12DescriptorHandle uav = alloc.Allocate();
        CreateTexture2DUav(*core, output, uav.cpu);

        // ---- 6. Compute パイプライン（テンプレ RootSig: SRV1 + UAV1 + 定数2）----
        ComputePipelineDesc pd;
        pd.numSrvs               = 1;   // t0
        pd.numUavs               = 1;   // u0
        pd.numRootConstantValues = 2;   // b0: width, height

        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);

        // ---- リードバック用バッファのフットプリント ----
        D3D12_RESOURCE_DESC outDesc = output.GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT   numRows  = 0;
        UINT64 rowSize  = 0;
        UINT64 totalBytes = 0;
        core->GetDevice()->GetCopyableFootprints(
            &outDesc, 0, 1, 0, &fp, &numRows, &rowSize, &totalBytes);

        D3D12ReadbackBuffer readback;
        readback.Initialize(core->GetDevice(), totalBytes);

        // ---- 7 & 8. Dispatch + Readback コピーを 1 つのコマンドリストに積む ----
        D3D12CommandContext ctx = core->CreateDirectContext();
        ctx.Reset();
        auto* cl = ctx.GetCommandList();

        ID3D12DescriptorHeap* heaps[] = { alloc.GetHeap() };
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetComputeRootSignature(pipeline.GetRootSignature());
        cl->SetPipelineState(pipeline.GetPipelineState());

        cl->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srv.gpu);
        cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);

        UINT consts[2] = { kWidth, kHeight };
        cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 2, consts, 0);

        const UINT gx = (kWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
        const UINT gy = (kHeight + kThreadGroupSize - 1) / kThreadGroupSize;
        cl->Dispatch(gx, gy, 1);

        // 出力 UAV → COPY_SOURCE
        ctx.ResourceBarrier(MakeTransitionBarrier(
            output.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        output.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);

        // テクスチャ → リードバックバッファ
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource       = readback.Get();
        dstLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint = fp;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource        = output.Get();
        srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        cl->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        ctx.Close();

        // ---- 実行して完了を待つ ----
        ID3D12CommandList* lists[] = { ctx.GetCommandList() };
        core->DirectQueue().ExecuteCommandLists(1, lists);
        UINT64 fv = core->DirectQueue().Signal();
        core->DirectQueue().WaitForFenceValue(fv);

        // ---- 9. 結果を検証 ----
        const auto* base = static_cast<const uint8_t*>(readback.Map());

        int    mismatches = 0;
        int    maxDiff    = 0;
        for (UINT y = 0; y < kHeight; ++y) {
            const uint8_t* row = base + static_cast<size_t>(y) * fp.Footprint.RowPitch;
            for (UINT x = 0; x < kWidth; ++x) {
                size_t  si = (static_cast<size_t>(y) * kWidth + x) * 4;
                uint8_t expected = ExpectedLuma(srcImage[si + 0],
                                                srcImage[si + 1],
                                                srcImage[si + 2]);
                uint8_t got = row[x * 4 + 0];   // R == G == B のはず

                int diff = std::abs(static_cast<int>(got) - static_cast<int>(expected));
                if (diff > maxDiff) maxDiff = diff;
                if (diff > 1) ++mismatches;     // 丸め誤差 ±1 は許容
            }
        }
        readback.Unmap();

        std::cout << "Dispatched " << gx << " x " << gy << " thread groups.\n";
        std::cout << "Readback row pitch: " << fp.Footprint.RowPitch
                  << " bytes (image width = " << kWidth * 4 << " bytes)\n";
        std::cout << "Max per-channel diff vs CPU reference: " << maxDiff << "\n";

        if (mismatches == 0) {
            std::cout << "RESULT: OK - GPU grayscale matches CPU reference.\n";
            core->WaitIdle();
            return 0;
        } else {
            std::cout << "RESULT: FAILED - " << mismatches << " pixels differ.\n";
            core->WaitIdle();
            return 2;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
