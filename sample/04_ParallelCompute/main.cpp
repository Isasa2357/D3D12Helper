//
// 04_ParallelCompute / main.cpp
//
// 1 つの D3D12Core（device / queue）を複数スレッドで共有して使うサンプル。
// D3D12 マルチスレッドの定石「各スレッドが自分のコマンドリストを並列に記録し、
// メインスレッドがまとめて実行する」を示す。
//
// このライブラリのスレッド安全性（ヘッダ記載）に沿った設計:
//   - ID3D12Device の生成系はスレッドセーフ → 各スレッドが Context/Resource を作ってよい
//   - D3D12DescriptorAllocator は非スレッドセーフ → スレッドごとに 1 つ持つ
//   - 1 コマンドリストの記録は単一スレッドから
//   - D3D12Fence::Signal はキューを独占する 1 スレッド（= main）から呼ぶ
//   - 一方で IsCompleted / GetCurrentValue は別スレッドから読んでも安全
//
// 各ワーカーは「自分専用の出力テクスチャを、自分の値で塗りつぶす」compute を
// 自分のコマンドリストに記録する。共有する ComputePipeline は read-only なので安全。
// main は全リストを 1 回の ExecuteCommandLists で実行し、1 回だけ Signal して待つ。
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace D3D12CoreLib;

namespace {

constexpr UINT kThreads   = 4;
constexpr UINT kTexSize   = 64;     // 各スレッドの出力テクスチャ（64x64）
constexpr UINT kGroup     = 8;

// u0 を gValue/255 で塗りつぶすだけの compute。
const char* kFillHlsl = R"(
RWTexture2D<float4> gOutput : register(u0);
cbuffer Constants : register(b0) { uint gValue; uint gWidth; uint gHeight; }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight) return;
    float v = gValue / 255.0f;
    gOutput[id.xy] = float4(v, v, v, 1.0f);
}
)";

// 1 スレッド分の作業データ。
struct ThreadWork {
    D3D12CommandContext      ctx;
    D3D12DescriptorAllocator alloc;
    D3D12Resource            output;
    D3D12ReadbackBuffer      readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT   rows  = 0;
    UINT   value = 0;
};

} // namespace

int main() {
    try {
        // ---- 共有 Core ----
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        config.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: " << core->DeviceContext().GetAdapterName() << L"\n";

        // ---- 共有 Compute パイプライン（read-only に全スレッドで共有）----
        ShaderBytecode cs = CompileShaderFromSource_Dxc(kFillHlsl, "main", "cs_6_0");
        ComputePipelineDesc pd;
        pd.numUavs               = 1;   // u0
        pd.numRootConstantValues = 3;   // value, width, height
        D3D12ComputePipeline pipeline;
        pipeline.InitializeWithTemplate(core->GetDevice(), cs, pd);

        // ---- スレッドごとの作業領域を準備（生成は main 側でまとめて）----
        std::vector<ThreadWork> works(kThreads);
        for (UINT i = 0; i < kThreads; ++i) {
            works[i].value = 20 + i * 50;   // スレッドごとに区別できる値

            works[i].ctx = core->CreateDirectContext();
            works[i].alloc.Initialize(core->GetDevice(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                      4, /*shaderVisible*/ true);
            works[i].output = CreateTexture2D(
                *core, kTexSize, kTexSize, DXGI_FORMAT_R8G8B8A8_UNORM,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            D3D12_RESOURCE_DESC d = works[i].output.GetDesc();
            UINT64 rowSize = 0, total = 0;
            core->GetDevice()->GetCopyableFootprints(
                &d, 0, 1, 0, &works[i].footprint, &works[i].rows, &rowSize, &total);
            works[i].readback.Initialize(core->GetDevice(), total);
        }

        // ---- 各スレッドが自分のコマンドリストを並列に記録する ----
        auto record = [&](UINT i) {
            ThreadWork& w = works[i];
            w.ctx.Reset();
            auto* cl = w.ctx.GetCommandList();

            ID3D12DescriptorHeap* heaps[] = { w.alloc.GetHeap() };
            cl->SetDescriptorHeaps(1, heaps);
            cl->SetComputeRootSignature(pipeline.GetRootSignature());
            cl->SetPipelineState(pipeline.GetPipelineState());

            D3D12DescriptorHandle uav = w.alloc.Allocate();
            CreateTexture2DUav(*core, w.output, uav.cpu);
            cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);

            UINT consts[3] = { w.value, kTexSize, kTexSize };
            cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 3, consts, 0);

            cl->Dispatch((kTexSize + kGroup - 1) / kGroup,
                         (kTexSize + kGroup - 1) / kGroup, 1);

            // UAV -> COPY_SOURCE してリードバックへ
            w.ctx.ResourceBarrier(MakeTransitionBarrier(
                w.output.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE));

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource       = w.readback.Get();
            dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = w.footprint;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource        = w.output.Get();
            src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            w.ctx.Close();
        };

        std::vector<std::thread> threads;
        for (UINT i = 0; i < kThreads; ++i) threads.emplace_back(record, i);
        for (auto& t : threads) t.join();

        // ---- main が全リストをまとめて実行し、1 回だけ Signal して待つ ----
        std::vector<ID3D12CommandList*> lists;
        for (auto& w : works) lists.push_back(w.ctx.GetCommandList());

        core->DirectQueue().ExecuteCommandLists(
            static_cast<UINT>(lists.size()), lists.data());
        UINT64 fv = core->DirectQueue().Signal();
        core->DirectQueue().WaitForFenceValue(fv);

        // ---- 各スレッドの出力を検証 ----
        bool ok = true;
        for (UINT i = 0; i < kThreads; ++i) {
            ThreadWork& w = works[i];
            const auto* p = static_cast<const uint8_t*>(w.readback.Map());
            // 中央ピクセルを確認（R == value のはず）
            const uint8_t* row = p + static_cast<size_t>(kTexSize / 2) * w.footprint.Footprint.RowPitch;
            uint8_t got = row[(kTexSize / 2) * 4 + 0];
            w.readback.Unmap();

            int diff = std::abs(static_cast<int>(got) - static_cast<int>(w.value));
            std::cout << "thread " << i << " : expected " << w.value
                      << ", got " << static_cast<int>(got)
                      << (diff <= 1 ? "  [OK]" : "  [MISMATCH]") << "\n";
            if (diff > 1) ok = false;
        }

        std::cout << (ok ? "RESULT: OK - all threads produced correct output.\n"
                         : "RESULT: FAILED.\n");
        core->WaitIdle();
        return ok ? 0 : 2;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
