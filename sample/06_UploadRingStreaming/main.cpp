//
// 06_UploadRingStreaming / main.cpp
//
// 毎フレーム CPU からテクスチャを更新する「ホットパス」を D3D12UploadRing で回すサンプル。
// ウィンドウは出さず、複数フレームを在庫（in-flight）させながらリングの使用量を表示する。
//
//   各フレーム:
//     1. このスロットの前回フレーム完了を待つ（コンテキスト再利用のため）
//     2. ring.ReclaimCompleted で GPU 完了済み領域を回収
//     3. RecordUploadTexture2D(Ring 版) でコマンドだけ積む（Wait しない）
//     4. Execute -> Signal -> ring.FinishFrame(fv)
//
// 重要: Ring 版 RecordUploadTexture2D は「dst が COPY_DEST 状態」を前提にコピーし、
//       finalState へ遷移する。毎フレーム再アップロードするので、2 フレーム目以降は
//       冒頭で finalState -> COPY_DEST に戻している。
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace D3D12CoreLib;

namespace {

constexpr UINT kWidth          = 256;
constexpr UINT kHeight         = 256;
constexpr UINT kFramesInFlight = 3;
constexpr UINT kNumFrames      = 120;
constexpr auto kFmt            = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr auto kFinalState     = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

// frame 番号に応じて色が動くグラデーションを生成。
void FillFrame(std::vector<uint8_t>& buf, UINT frame) {
    for (UINT y = 0; y < kHeight; ++y) {
        for (UINT x = 0; x < kWidth; ++x) {
            size_t i = (static_cast<size_t>(y) * kWidth + x) * 4;
            buf[i + 0] = static_cast<uint8_t>(x + frame);
            buf[i + 1] = static_cast<uint8_t>(y + frame * 2);
            buf[i + 2] = static_cast<uint8_t>(frame);
            buf[i + 3] = 255;
        }
    }
}

} // namespace

int main() {
    try {
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        config.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(config);
        ID3D12Device* device = core->GetDevice();
        D3D12Queue&   queue  = core->DirectQueue();

        std::wcout << L"Adapter: " << core->DeviceContext().GetAdapterName() << L"\n";

        // ---- ストリーミング先テクスチャ（COPY_DEST で作る）----
        D3D12Resource dst = CreateTexture2D(
            *core, kWidth, kHeight, kFmt, D3D12_RESOURCE_STATE_COPY_DEST);

        // 1 フレーム分の必要サイズからリングサイズを決める。
        const UINT64 perFrame = GetRequiredUploadSize(*core, dst);
        const UINT64 ringSize = perFrame * (kFramesInFlight + 1) + (1u << 20);  // 余裕を持たせる

        D3D12UploadRing ring;
        ring.Initialize(device, ringSize);

        std::cout << "Per-frame upload size : " << perFrame << " bytes\n";
        std::cout << "Ring size             : " << ring.GetRingSize() << " bytes\n\n";

        // ---- 在庫数ぶんの記録コンテキストとフレーム Fence 値 ----
        std::array<D3D12CommandContext, kFramesInFlight> contexts;
        std::array<UINT64, kFramesInFlight>              frameFv{};   // 0 初期化
        for (auto& c : contexts) c = core->CreateDirectContext();

        std::vector<uint8_t> frameData(static_cast<size_t>(kWidth) * kHeight * 4);

        // ---- ストリーミングループ ----
        for (UINT frame = 0; frame < kNumFrames; ++frame) {
            const UINT slot = frame % kFramesInFlight;

            // 1. このスロットの前回フレーム完了待ち
            queue.WaitForFenceValue(frameFv[slot]);

            // 2. 完了済み領域を回収
            ring.ReclaimCompleted(queue.Fence());

            // 3. コマンド記録
            D3D12CommandContext& ctx = contexts[slot];
            ctx.Reset();

            // 2 フレーム目以降: finalState -> COPY_DEST に戻す
            if (dst.GetState() != D3D12_RESOURCE_STATE_COPY_DEST) {
                ctx.ResourceBarrier(MakeTransitionBarrier(
                    dst.Get(), dst.GetState(), D3D12_RESOURCE_STATE_COPY_DEST));
                dst.SetState(D3D12_RESOURCE_STATE_COPY_DEST);
            }

            FillFrame(frameData, frame);
            RecordUploadTexture2D(*core, ctx, dst, ring,
                                  frameData.data(), kWidth, kHeight, kFmt,
                                  /*srcRowPitch*/ 0, kFinalState);
            // ここで本来は dst を SRV としてサンプルする描画コマンドが入る。

            ctx.Close();

            // 4. Execute -> Signal -> FinishFrame
            ID3D12CommandList* lists[] = { ctx.GetCommandList() };
            queue.ExecuteCommandLists(1, lists);
            UINT64 fv = queue.Signal();
            frameFv[slot] = fv;
            ring.FinishFrame(fv);

            if (frame % 30 == 0) {
                std::cout << "frame " << frame
                          << " : ring used = " << ring.GetUsedBytes()
                          << " / free = "       << ring.GetFreeBytes() << " bytes\n";
            }
        }

        core->WaitIdle();
        ring.ReclaimCompleted(queue.Fence());

        std::cout << "\nStreamed " << kNumFrames << " frames.\n";
        std::cout << "Ring used after drain : " << ring.GetUsedBytes() << " bytes\n";
        std::cout << "RESULT: OK\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
