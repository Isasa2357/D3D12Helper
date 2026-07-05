# D3D12Helper ドキュメント

D3D12Helper は、Direct3D 12 の定型処理（デバイス生成・キュー/フェンス管理・コマンド記録・リソース/ディスクリプタ生成・アップロード/リードバック・Compute / Graphics パイプライン・共有リソース）を薄くラップした **Layer 1 / Layer 2 / Layer 3 構成の C++17 ヘルパライブラリ**です。

- すべての型・関数は名前空間 `D3D12CoreLib` に属します。
- Processing Layer の型・関数は `D3D12CoreLib::Processing` に属します。
- 対象環境は Windows / MSVC、Direct3D 12 + DXGI 1.6 です。
- ライブラリ本体は `include/` と `src/` に分かれます。

ドキュメントは次のファイルに分かれています。

| ファイル | 内容 |
| --- | --- |
| `README.md`（本書） | 全体像・設計思想・ファイル構成・組み込み方法・クイックスタート |
| [`D3D12Core.md`](D3D12Core.md) | Layer 1（`D3D12Core`）の API リファレンス |
| [`D3D12Framework.md`](D3D12Framework.md) | Layer 2（`D3D12Framework`）の API リファレンス |
| [`D3D12Processing.md`](D3D12Processing.md) | Layer 3（`D3D12Processing`）の API リファレンス |
| [`Patterns.md`](Patterns.md) | よくある処理パターン（レシピ集） |

実際に動くコードは [`../sample`](../sample) を参照してください。

---

## 設計思想

### 3 レイヤー構成

```text
┌──────────────────────────────────────────────────────────────┐
│  アプリ / 上位サブシステム（Window, Camera, Renderer, ...）  │
└──────────────────────────────────────────────────────────────┘
                  │ 1 つの D3D12Core を shared_ptr で共有
                  ▼
┌──────────────────────────────────────────────────────────────┐
│  Layer 3 : D3D12Processing                                   │
│    FormatConvert / Resize / Remap / Composite / Fused pass    │
│    NV12 / P010 plane view、色空間変換、Processing shader      │
└──────────────────────────────────────────────────────────────┘
                  │ Layer 2 の Resource / Descriptor / Pipeline を使う
                  ▼
┌──────────────────────────────────────────────────────────────┐
│  Layer 2 : D3D12Framework                                    │
│    ステートフルな building blocks                            │
│      D3D12Resource / DescriptorHeap / DescriptorAllocator     │
│      UploadBuffer / UploadRing / ReadbackBuffer               │
│      ComputePipeline / GraphicsPipeline / ShaderCompiler      │
│    D3D12Core& を取る自由関数群                                │
│      CreateBuffer / CreateTexture2D / CreateStructuredBuffer  │
│      CreateConstantBuffer / CreateSharedTexture2D             │
│      CreateTexture2DSrv/Uav / CreateBufferSrv/Uav / CBV       │
│      CreateTexture2DRtv/Dsv / RecordUploadTexture2D           │
│      SwapChain ヘルパ                                         │
└──────────────────────────────────────────────────────────────┘
                  │ Core& を受け取って使う
                  ▼
┌──────────────────────────────────────────────────────────────┐
│  Layer 1 : D3D12Core（ファサード）                            │
│    D3D12Core                                                 │
│      ├─ D3D12DeviceContext (Factory / Adapter / Device / LUID)│
│      ├─ D3D12Queue × N      (Direct / Copy / Compute)         │
│      │     └─ D3D12Fence    (値管理・GPU 完了待ち・Queue 間同期)│
│      └─ D3D12CommandContext (Allocator + List, Reset/Close)   │
│    補助: DxgiAdapterSelector / Barrier / FormatUtil / Debug   │
│          ThrowIfFailed / SharedResource / DxgiUtil            │
└──────────────────────────────────────────────────────────────┘
```

- **Layer 1（D3D12Core）** は「デバイスとキューとフェンスとコマンド記録口」を 1 つに束ねるファサードです。Descriptor やリソースの生成は持ちません。
- **Layer 2（D3D12Framework）** は、サブシステムが所有して使う「ステートフルな部品」と、`D3D12Core&` を第 1 引数に取る「自由関数群」で構成されます。
- **Layer 3（D3D12Processing）** は、画像処理に特化した compute shader pass 群です。Format conversion、resize、remap、composite、fused convert+resize を提供します。

### 共有方針：1 つの Core を全サブシステムで共有

`D3D12Core::CreateShared()` は `std::shared_ptr<D3D12Core>` を返します。Window・Camera・Renderer・Processing といったサブシステムは、この 1 つの Core を共有保持して使う想定です。Descriptor Allocator だけはサブシステムごとに必要なだけ生成します（Core は所有しません）。

### 明示的バリア・単一状態追跡

- リソース遷移は基本「明示的」です。`MakeTransitionBarrier()` などでバリアを作り、`D3D12CommandContext::ResourceBarrier()` で積みます。
- `D3D12Resource` は「現在の状態」を **1 つだけ** 追跡する簡易ラッパです。サブリソースごとに状態が分かれるケース（mip / array / planar）、複数キュー同時参照、外部 API 共有では、必要に応じて before/after を明示して手動管理してください。

### 例外ベースのエラー処理

失敗した `HRESULT` は `D3D12CORE_THROW_IF_FAILED()` で例外化されます。Processing Layer では validation error / unsupported format / unsupported feature も例外として通知します。

---

## ディレクトリ構成

```text
D3D12Helper/
├─ include/D3D12Helper/
│  ├─ D3D12Core/          … Layer 1
│  │   D3D12Common.hpp / D3D12Core.hpp / D3D12CoreConfig.hpp
│  │   D3D12DeviceContext.hpp / D3D12Queue.hpp / D3D12Fence.hpp
│  │   D3D12CommandContext.hpp / D3D12Barrier.hpp / D3D12Debug.hpp
│  │   D3D12FormatUtil.hpp / D3D12SharedResource.hpp
│  │   DxgiAdapterSelector.hpp / DxgiUtil.hpp / ThrowIfFailed.hpp
│  ├─ D3D12Framework/     … Layer 2
│  │   D3D12Framework.hpp / D3D12Resource.hpp
│  │   D3D12DescriptorHeap.hpp / D3D12DescriptorAllocator.hpp
│  │   D3D12UploadBuffer.hpp / D3D12UploadRing.hpp
│  │   D3D12ReadbackBuffer.hpp / D3D12ComputePipeline.hpp
│  │   D3D12GraphicsPipeline.hpp / D3D12ShaderCompiler.hpp
│  │   D3D12Helpers.hpp / D3D12SwapChainHelper.hpp
│  └─ D3D12Processing/    … Layer 3
│      D3D12Processing.hpp / D3D12ProcessingTypes.hpp
│      D3D12ProcessingContext.hpp / D3D12TextureViews.hpp
│      D3D12FormatConverter.hpp / D3D12Resize.hpp
│      D3D12Remap.hpp / D3D12Composite.hpp / D3D12FusedPipeline.hpp
├─ shaders/D3D12Processing/ … Processing HLSL / hlsli
├─ src/            … ライブラリ本体の実装
├─ doc/            … 本ドキュメント
├─ sample/         … 使用例
├─ test/           … 機能別テスト（CTest 連携）
└─ CMakeLists.txt  … ルートビルド（ライブラリ + サンプル + テスト）
```

---

## 組み込み方法

このライブラリは **ヘッダ + .cpp + HLSL shader** のソース構成です。CMake なら `add_subdirectory` で組み込めます。

```cmake
add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

CMake を使わず Visual Studio プロジェクトに直接追加する場合は、次を追加します。

1. インクルードパスに `include/` と `include/D3D12Helper/` を通す。
2. `src/*.cpp` をコンパイル対象に追加する。
3. DXC を使う場合は `dxcompiler.dll` / `dxil.dll` を実行ファイル横または PATH 上に置く。
4. Processing Layer を使う場合は `shaders/D3D12Processing/` を実行時に参照できる場所へ配置する。

短い include 形式:

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
#include "D3D12Processing/D3D12Processing.hpp"
```

prefix 付き include 形式:

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
```

---

## クイックスタート

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <iostream>

using namespace D3D12CoreLib;

int main() {
    try {
        D3D12CoreConfig config;
        config.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: "
                   << core->DeviceContext().GetAdapterName() << L"\n";

        D3D12Resource buffer = CreateBuffer(
            *core,
            1024,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON);

        core->WaitIdle();
    } catch (const std::exception& e) {
        std::cerr << "D3D12 init failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

GPU で Compute を回して結果を CPU に戻す例は [`../sample/02_ComputeGrayscale`](../sample/02_ComputeGrayscale) と [`Patterns.md`](Patterns.md) を参照してください。Processing Layer の例は [`D3D12Processing.md`](D3D12Processing.md) と [`../sample`](../sample) を参照してください。
