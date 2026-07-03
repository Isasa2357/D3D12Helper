//
// 01_HelloDevice / main.cpp
//
// D3D12Helper の最小サンプル。
//   - D3D12CoreConfig を指定して D3D12Core を初期化する
//   - アダプタ名・対応機能・生成されたキューを表示する
//   - SetDebugName でオブジェクトに名前を付ける
//   - 終了前に WaitIdle で全キューをフラッシュする
//
// ウィンドウも GPU 実行も不要なので、D3D12 が動く環境かどうかの確認にも使える。
//
#include "D3D12Core/D3D12Core.hpp"    // インクルードパスに include/ を通すこと
#include "D3D12Core/D3D12Debug.hpp"   // D3D12Debug::SetDebugName
#include "D3D12Core/DxgiUtil.hpp"     // LuidToWString

#include <iostream>
#include <string>

using namespace D3D12CoreLib;

namespace {

// D3D12_COMMAND_LIST_TYPE を読みやすい文字列にする。
const char* QueueTypeName(D3D12_COMMAND_LIST_TYPE type) {
    switch (type) {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:  return "Direct";
        case D3D12_COMMAND_LIST_TYPE_COMPUTE: return "Compute";
        case D3D12_COMMAND_LIST_TYPE_COPY:    return "Copy";
        default:                              return "Other";
    }
}

} // namespace

int main() {
    try {
        // --- 初期化設定 ---
        D3D12CoreConfig config;
        config.enableDebugLayer    = true;   // 開発時は ON（Debug 層が無ければ自動で無視される）
        config.enableGpuValidation = false;  // 重いので必要なときだけ
        config.enableInfoQueue     = true;
        config.enableDred          = true;

        config.preferHighPerformanceAdapter = true;
        config.allowWarpAdapter             = true;   // HW が無ければ WARP で続行

        config.createDirectQueue  = true;    // Direct はどのみち必ず作られる
        config.createCopyQueue    = true;
        config.createComputeQueue = true;    // 専用 Compute Queue も作ってみる

        // --- 生成（1 つの Core を全サブシステムで共有する想定の shared_ptr）---
        std::shared_ptr<D3D12Core> core = D3D12Core::CreateShared(config);

        // デバッガ / PIX 上の表示名を付ける
        D3D12Debug::SetDebugName(core->GetDevice(),             L"HelloDevice.Device");
        D3D12Debug::SetDebugName(core->GetDirectCommandQueue(), L"HelloDevice.DirectQueue");

        // --- アダプタ情報 ---
        D3D12DeviceContext& dc = core->DeviceContext();

        std::wcout << L"==== D3D12Helper / HelloDevice ====\n";
        std::wcout << L"Adapter      : " << dc.GetAdapterName() << L"\n";

        const LUID luid = dc.GetAdapterLuid();
        std::wcout << L"Adapter LUID : " << LuidToWString(luid) << L"\n";

        std::wcout << L"Resource sharing supported : "
                   << (dc.SupportsResourceSharing() ? L"yes" : L"no") << L"\n";
        std::wcout << L"Typed UAV load (R32_FLOAT) : "
                   << (dc.SupportsTypedUavLoad(DXGI_FORMAT_R32_FLOAT) ? L"yes" : L"no")
                   << L"\n";

        // --- キュー ---
        std::cout << "\nQueues:\n";
        std::cout << "  - " << QueueTypeName(core->DirectQueue().GetType())
                  << " (always created)\n";
        std::cout << "  - " << QueueTypeName(core->CopyQueue().GetType()) << "\n";
        if (core->ComputeQueue()) {
            std::cout << "  - " << QueueTypeName(core->ComputeQueue()->GetType()) << "\n";
        } else {
            std::cout << "  - (no dedicated compute queue)\n";
        }

        // --- 何も積まずに各キューをフラッシュできることの確認 ---
        core->WaitIdle();

        std::cout << "\nDevice is ready. (no GPU work was submitted)\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "D3D12 initialization failed: " << e.what() << "\n";
        return 1;
    }
}
