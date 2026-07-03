# D3D12Helper ドキュメント

D3D12Helper は、Direct3D 12 の定型処理（デバイス生成・キュー/フェンス管理・コマンド記録・リソース/ディスクリプタ生成・アップロード/リードバック・Compute パイプライン）を薄くラップした **2 レイヤー構成の C++ ヘルパライブラリ**です。

- すべての型・関数は名前空間 `D3D12CoreLib` に属します。
- 文字コードはソースが UTF-16LE（BOM 付き）です。サンプルは UTF-8 で提供しています。
- 対象環境は Windows / MSVC、Direct3D 12 + DXGI 1.6 です。

ドキュメントは次の 4 つに分かれています。

| ファイル | 内容 |
| --- | --- |
| `README.md`（本書） | 全体像・設計思想・ファイル構成・組み込み方法・クイックスタート |
| [`D3D12Core.md`](D3D12Core.md) | Layer 1（`include/D3D12Core`）の API リファレンス |
| [`D3D12Framework.md`](D3D12Framework.md) | Layer 2（`include/D3D12Framework`）の API リファレンス |
| [`Patterns.md`](Patterns.md) | よくある処理パターン（レシピ集） |

実際に動くコードは [`../sample`](../sample) を参照してください。

---

## 設計思想

### 2 レイヤー構成

```
┌──────────────────────────────────────────────────────────────┐
│  アプリ / 上位サブシステム（Window, Camera, Renderer, ...）  │
└──────────────────────────────────────────────────────────────┘
                  │ 1 つの D3D12Core を shared_ptr で共有
                  ▼
┌──────────────────────────────────────────────────────────────┐
│  Layer 2 : D3D12Framework                                    │
│    ステートフルな building blocks                            │
│      D3D12Resource / DescriptorHeap / DescriptorAllocator     │
│      UploadBuffer / UploadRing / ReadbackBuffer               │
│      ComputePipeline / ShaderCompiler                         │
│    D3D12Core& を取る自由関数群                                │
│      CreateBuffer / CreateTexture2D... / RecordUploadTexture2D │
│      CreateTexture2DSrv/Uav / SwapChain ヘルパ               │
└──────────────────────────────────────────────────────────────┘
                  │ Core& を受け取って使う
                  ▼
┌──────────────────────────────────────────────────────────────┐
│  Layer 1 : D3D12Core（ファサード）                          │
│    D3D12Core                                                 │
│      ├─ D3D12DeviceContext (Factory / Adapter / Device / LUID)│
│      ├─ D3D12Queue × N      (Direct / Copy / Compute)         │
│      │     └─ D3D12Fence    (値管理・GPU 完了待ち・Queue 間同期)│
│      └─ D3D12CommandContext (Allocator + List, Reset/Close)   │
│    補助: DxgiAdapterSelector / Barrier / FormatUtil / Debug   │
│          ThrowIfFailed / SharedResource / DxgiUtil           │
└──────────────────────────────────────────────────────────────┘
```

- **Layer 1（D3D12Core）** は「デバイスとキューとフェンスとコマンド記録口」を 1 つに束ねるファサードです。Descriptor やリソースの生成は持ちません（それは Layer 2 の役割）。
- **Layer 2（D3D12Framework）** は、サブシステムが所有して使う「ステートフルな部品」と、`D3D12Core&` を第 1 引数に取る「自由関数群」で構成されます。上位コードは *Core を 1 つ渡して 1 行呼ぶ* だけでリソースを得られます。

### 共有方針：1 つの Core を全サブシステムで共有

`D3D12Core::CreateShared()` は `std::shared_ptr<D3D12Core>` を返します。Window・Camera・各種 Renderer といったサブシステムは、この 1 つの Core を共有保持して使う想定です。Descriptor Allocator だけはサブシステムごとに必要なだけ生成します（Core は所有しません）。

### 明示的バリア・単一状態追跡

- リソース遷移は基本「明示的」です。`MakeTransitionBarrier()` などでバリアを作り、`D3D12CommandContext::ResourceBarrier()` で積みます。
- `D3D12Resource` は「現在の状態」を **1 つだけ** 追跡する簡易ラッパです。サブリソースごとに状態が分かれるケース（mip / array / planar）、複数キュー同時参照、外部 API 共有では正しく機能しないため、その場合は before/after を明示して手動管理してください。

### 例外ベースのエラー処理

失敗した `HRESULT` は `D3D12CORE_THROW_IF_FAILED()` で例外化されます。例外には HRESULT 値・呼び出し式・ファイル・行が含まれます。アプリ側は `try/catch (const std::exception&)` で受けてください。

### 同期モデル

- 各 `D3D12Queue` は専用の `D3D12Fence` を 1 つ持ちます。`Signal()` は値をインクリメントして返し、`WaitForFenceValue()` / `WaitIdle()` で完了を待ちます。
- キュー間同期は `GpuWait()`（GPU 側待ち）で行います。
- `D3D12Fence` は move-only で、`m_nextValue` が `std::atomic` のため「あるスレッドが Signal、別スレッドが IsCompleted/GetCurrentValue を読む」cross-thread アクセスは安全です。ただし `Signal()` 自体は 1 スレッドから呼ぶ前提です。

---

## ディレクトリ構成

```
D3D12Helper/
├─ include/
│  ├─ D3D12Core/          … Layer 1
│  │   D3D12Common.hpp            共通 include / ComPtr エイリアス
│  │   D3D12CoreConfig.hpp        初期化設定
│  │   D3D12Core.{hpp,cpp}        ファサード
│  │   D3D12DeviceContext.{hpp,cpp}  Factory/Adapter/Device/LUID
│  │   D3D12Queue.{hpp,cpp}       Queue + 専用 Fence
│  │   D3D12Fence.{hpp,cpp}       Fence 値管理・完了待ち・Queue 間同期
│  │   D3D12CommandContext.{hpp,cpp}  Allocator + List（Reset/Close）
│  │   DxgiAdapterSelector.{hpp,cpp}  Factory 生成・アダプタ選択
│  │   D3D12Barrier.{hpp,cpp}     Resource Barrier 生成
│  │   D3D12FormatUtil.{hpp,cpp}  DXGI_FORMAT 判定
│  │   D3D12Debug.{hpp,cpp}       Debug 層 / GPU Validation / InfoQueue / DRED
│  │   ThrowIfFailed.{hpp,cpp}    HRESULT 例外化
│  │   D3D12SharedResource.{hpp,cpp}  共有 Handle 作成/オープン
│  │   DxgiUtil.{hpp,cpp}         LUID ユーティリティ
│  └─ D3D12Framework/     … Layer 2
│      D3D12Framework.hpp         まとめ include
│      D3D12Resource.hpp          ID3D12Resource + 状態
│      D3D12DescriptorHandle.hpp  CPU/GPU ハンドルペア
│      D3D12DescriptorHeap.{hpp,cpp}   Descriptor Heap
│      D3D12DescriptorAllocator.{hpp,cpp}  線形 Allocator
│      D3D12UploadBuffer.{hpp,cpp}     Upload Heap（persistent map）
│      D3D12UploadRing.{hpp,cpp}       高 fps 用リングバッファ
│      D3D12ReadbackBuffer.{hpp,cpp}   Readback Heap
│      D3D12ComputePipeline.{hpp,cpp}  Compute RootSig + PSO
│      D3D12GraphicsPipeline.{hpp,cpp} Graphics PSO（既定値 + カスタマイズ）
│      D3D12ShaderCompiler.{hpp,cpp}   .cso 読込 / D3DCompile / DXC
│      D3D12Helpers.{hpp,cpp}     Core& を取る自由関数群
│      D3D12SwapChainHelper.{hpp,cpp}  SwapChain 作成ヘルパ
├─ doc/      … 本ドキュメント
└─ sample/   … 使用例
```

---

## 組み込み方法

このライブラリは **ヘッダ + .cpp のソース構成**で、事前ビルド済みの `.lib` はありません。利用側のビルドに `include/**/*.cpp` を一緒にコンパイルして組み込んでください。

リンクするシステムライブラリは `D3D12Common.hpp` 内の `#pragma comment(lib, ...)` で MSVC 向けに自動指定されます。

```cpp
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")
```

DXC（SM 6.0 以降）でランタイムコンパイルする場合は、実行時に `dxcompiler.dll`（必要に応じ `dxil.dll`）が PATH 上に必要です。`D3DCompile`（SM 5.1 まで）の `d3dcompiler_47.dll` は OS 同梱です。

インクルードパスに `include/` を通せば、次のように使えます。

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
```

ビルドの具体例は [`../sample/CMakeLists.txt`](../sample/CMakeLists.txt) を参照してください。

---

## クイックスタート

最小のデバイス初期化からアダプタ名表示まで。

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include <iostream>

using namespace D3D12CoreLib;

int main() {
    try {
        D3D12CoreConfig config;          // 既定: Debug 層 ON, Direct + Copy キュー
        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: "
                   << core->DeviceContext().GetAdapterName() << L"\n";

        // ... ここで Framework の関数を使ってリソース生成・描画・Compute ...

        core->WaitIdle();                // 終了前に全キューをフラッシュ
    } catch (const std::exception& e) {
        std::cerr << "D3D12 init failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

GPU で Compute を回して結果を CPU に戻す、より実践的な例は
[`../sample/02_ComputeGrayscale`](../sample/02_ComputeGrayscale) と
[`Patterns.md`](Patterns.md) を参照してください。
