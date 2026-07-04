# D3D12Helper

Direct3D 12 の定型処理を薄くラップした **2 レイヤー構成の C++17 ヘルパライブラリ**です。

デバイス生成、DXGI アダプタ選択、Queue / Fence / CommandList 管理、リソース作成、Descriptor 管理、Upload / Readback、Compute / Graphics パイプライン、シェーダコンパイル、SwapChain 作成など、D3D12 アプリケーションで繰り返し書くコードを軽量クラスと自由関数にまとめています。

> 姉妹プロジェクト [D3D11Helper](https://github.com/Isasa2357/D3D11Helper) と近い設計思想で、D3D11 / D3D12 間の移植時に認知コストが小さくなるようにしています。

---

## 特徴

- **2 レイヤー分離**  
  Layer 1（Core: Device / Queue / Fence / CommandContext / DXGI）と Layer 2（Framework: Resource / Descriptor / Upload / Pipeline）を分離。
- **`D3D12Core&` を取る自由関数**  
  `CreateBuffer`、`CreateTexture2D`、`CreateTexture2DSrv`、`RecordUploadTexture2D` などを 1 行で呼べる。
- **Command Queue / Fence / CommandContext 管理**  
  Direct / Copy / Compute Queue と Fence 値管理、Command Allocator + Command List の Reset / Close を整理。
- **Descriptor 管理**  
  `D3D12DescriptorHeap` と `D3D12DescriptorAllocator` により、RTV / DSV / CBV_SRV_UAV / Sampler Heap の確保を簡略化。
- **Upload / Readback 補助**  
  `D3D12UploadBuffer`、`D3D12UploadRing`、`D3D12ReadbackBuffer` により、CPU↔GPU 転送を扱いやすくする。
- **ComputePipeline / GraphicsPipeline**  
  Root Signature / PSO / Shader の設定をまとめて管理。簡単な既定設定から、必要に応じた詳細指定まで対応。
- **ShaderCompiler**  
  `.cso` 読み込み、`D3DCompile`、DXC による HLSL コンパイルを扱う。
- **ThrowIfFailed**  
  HRESULT 失敗時に式・ファイル・行・メッセージ付きで例外化。
- **D3D11Helper と似た構成**  
  `include/`、`src/`、`sample/`、`test/`、`doc/` を分け、ライブラリ本体とサンプル・テストを混ぜない。

---

## アーキテクチャ

```text
+--------------------------------------------------------------+
|  アプリ / 上位サブシステム                                   |
|  Window / Camera / Renderer / Compute Task / Interop など    |
+--------------------------------------------------------------+
                    | shared_ptr<D3D12Core> を共有
                    v
+--------------------------------------------------------------+
|  Layer 2 : D3D12Framework                                    |
|    Building blocks                                           |
|      D3D12Resource                                           |
|      D3D12DescriptorHeap / D3D12DescriptorAllocator          |
|      D3D12UploadBuffer / D3D12UploadRing                     |
|      D3D12ReadbackBuffer                                     |
|      D3D12ComputePipeline / D3D12GraphicsPipeline            |
|      D3D12ShaderCompiler / D3D12SwapChainHelper              |
|    自由関数                                                  |
|      CreateBuffer / CreateTexture2D                          |
|      CreateTexture2DSrv / CreateTexture2DUav                 |
|      RecordUploadBuffer / RecordUploadTexture2D              |
+--------------------------------------------------------------+
                    |
                    v
+--------------------------------------------------------------+
|  Layer 1 : D3D12Core                                         |
|    D3D12Core                                                 |
|      +- D3D12DeviceContext                                   |
|      |    Factory / Adapter / Device / LUID                  |
|      +- D3D12Queue                                           |
|      |    Direct / Copy / Compute Queue + Fence              |
|      +- D3D12CommandContext                                  |
|           Command Allocator + Command List                   |
|    Utility                                                   |
|      ThrowIfFailed / DxgiAdapterSelector / DxgiUtil          |
|      D3D12Barrier / D3D12FormatUtil / D3D12Debug             |
|      D3D12Fence / D3D12SharedResource                        |
+--------------------------------------------------------------+
```

### 基本方針

`D3D12Core` は、D3D12 のデバイス・キュー・フェンス・コマンド記録口を束ねるファサードです。アプリケーション内では 1 つの `std::shared_ptr<D3D12Core>` を作成し、Renderer、Camera、Compute 処理など複数のサブシステムで共有することを想定しています。

Descriptor Allocator や Upload Ring などのフレームごと・用途ごとの状態を持つオブジェクトは、Layer 2 側で必要に応じて作成します。

---

## ディレクトリ構成

```text
D3D12Helper/
├── CMakeLists.txt                  # ルートビルド（ライブラリ + サンプル + テスト）
├── cmake/                          # CMake 補助モジュール
│   └── D3D12HelperDxcRuntime.cmake # dxcompiler.dll / dxil.dll コピー補助
├── include/D3D12Helper/
│   ├── D3D12Core/                  # Layer 1 ヘッダ
│   │   ├── D3D12Common.hpp
│   │   ├── D3D12Core.hpp
│   │   ├── D3D12CoreConfig.hpp
│   │   ├── D3D12DeviceContext.hpp
│   │   ├── D3D12Queue.hpp
│   │   ├── D3D12Fence.hpp
│   │   ├── D3D12CommandContext.hpp
│   │   ├── D3D12Barrier.hpp
│   │   ├── D3D12Debug.hpp
│   │   ├── D3D12FormatUtil.hpp
│   │   ├── D3D12SharedResource.hpp
│   │   ├── DxgiAdapterSelector.hpp
│   │   ├── DxgiUtil.hpp
│   │   └── ThrowIfFailed.hpp
│   └── D3D12Framework/             # Layer 2 ヘッダ
│       ├── D3D12Framework.hpp      # まとめ include
│       ├── D3D12Resource.hpp
│       ├── D3D12DescriptorHeap.hpp
│       ├── D3D12DescriptorAllocator.hpp
│       ├── D3D12DescriptorHandle.hpp
│       ├── D3D12UploadBuffer.hpp
│       ├── D3D12UploadRing.hpp
│       ├── D3D12ReadbackBuffer.hpp
│       ├── D3D12ComputePipeline.hpp
│       ├── D3D12GraphicsPipeline.hpp
│       ├── D3D12ShaderCompiler.hpp
│       ├── D3D12Helpers.hpp
│       └── D3D12SwapChainHelper.hpp
├── src/                            # ライブラリ本体の実装 (.cpp)
├── sample/                         # サンプルコード
│   ├── 01_HelloDevice/
│   ├── 02_ComputeGrayscale/
│   ├── 03_HelloTriangle/
│   ├── 04_ParallelCompute/
│   ├── 05_BufferCompute/
│   └── 06_UploadRingStreaming/
├── test/                           # テストコード（CTest 対応）
└── doc/                            # 詳細ドキュメント
```

ライブラリ本体は `include/` と `src/` に分離しています。  
`sample/` と `test/` はライブラリ利用時には必須ではないため、他プロジェクトへ組み込むときは基本的に `include/` と `src/` を追加すればよい構成です。

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
        config.enableDebugLayer = true;
        config.enableGpuValidation = false;
        config.allowWarpAdapter = true;

        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: "
                   << core->DeviceContext().GetAdapterName()
                   << L"\n";

        auto buffer = CreateBuffer(
            *core,
            1024,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_HEAP_TYPE_DEFAULT);

        core->WaitIdle();
    } catch (const std::exception& e) {
        std::cerr << "D3D12Helper error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
```

既存のサンプル・テストでは、短い include 形式も使えるようにしています。

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
```

CMake ターゲット `D3D12Helper::D3D12Helper` をリンクすると、どちらの include 形式も使えるように include path を設定しています。

---

## ビルド

### 必要環境

- Windows 10 / 11
- Visual Studio 2019 以降
- MSVC / C++17
- CMake 3.20 以降
- Direct3D 12 対応 GPU
  - WARP を許可する設定であれば、GPU がない環境でも一部サンプル・テストは動作可能です。
- DXC を使う場合は `dxcompiler.dll` / `dxil.dll`

### DXC ランタイムの準備

DXC を使うサンプル・テストでは、実行時に `dxcompiler.dll` が必要です。NuGet から取得する場合は、リポジトリ直下で次を実行します。

```bat
nuget install Microsoft.Direct3D.DXC -Version 1.9.2602.24 -OutputDirectory packages
```

CMake は `packages/` やユーザーの NuGet キャッシュから `dxcompiler.dll` / `dxil.dll` を探し、サンプル・テストの exe 横へコピーします。

見つからない場合は、DLL のあるディレクトリを直接指定できます。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_DXC_RUNTIME_DIR="C:\path\to\Microsoft.Direct3D.DXC\build\native\bin\x64"
```

### CMake ビルド

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Debug
```

Release ビルドにする場合は `--config Release` を指定します。

```bat
cmake --build out/build/default --config Release
```

### テスト

```bat
ctest --test-dir out/build/default -C Debug --output-on-failure
```

特定の suite だけ実行する場合：

```bat
ctest --test-dir out/build/default -C Debug -R ComputePipeline --output-on-failure
```

---

## CMake オプション

| オプション | 既定値 | 説明 |
| --- | --- | --- |
| `D3D12HELPER_BUILD_SAMPLES` | `ON` | `sample/` をビルドする |
| `D3D12HELPER_BUILD_TESTS` | `ON` | `test/` をビルドし、CTest に登録する |
| `D3D12HELPER_ENABLE_WARNINGS` | `ON` | MSVC の警告を有効化する |
| `D3D12HELPER_COPY_DXC_RUNTIME` | `ON` | `dxcompiler.dll` / `dxil.dll` を exe 横へコピーする |
| `D3D12HELPER_DXC_RUNTIME_DIR` | 空 | DXC DLL があるディレクトリを明示指定する |

ライブラリだけをビルドしたい場合：

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=OFF ^
  -DD3D12HELPER_BUILD_TESTS=OFF

cmake --build out/build/default --config Release
```

---

## サンプル

| 名前 | 概要 | ウィンドウ |
| --- | --- | --- |
| `01_HelloDevice` | D3D12Core を初期化し、アダプタ情報・キュー情報を表示 | 不要 |
| `02_ComputeGrayscale` | GPU で画像をグレースケール変換し、CPU リードバックで検証 | 不要 |
| `03_HelloTriangle` | Win32 ウィンドウに三角形を描画 | あり |
| `04_ParallelCompute` | 複数スレッドでコマンドを記録し、Compute を並列実行 | 不要 |
| `05_BufferCompute` | Structured / Buffer を使って SAXPY を GPU 計算 | 不要 |
| `06_UploadRingStreaming` | UploadRing を使って毎フレーム CPU→GPU テクスチャ更新 | 不要 |

詳細は [`sample/README.md`](sample/README.md) を参照してください。

---

## テスト

`test/` には、外部テストフレームワークに依存しない軽量テストが入っています。  
1 つの実行ファイル `d3d12helper_tests.exe` にまとめ、CTest から suite 単位で実行します。

主な suite：

- `FormatUtil`
- `DxgiUtil`
- `ThrowIfFailed`
- `Barrier`
- `Core`
- `Fence`
- `CommandContext`
- `DescriptorAllocator`
- `Resource`
- `UploadReadback`
- `UploadRing`
- `ShaderCompiler`
- `ComputePipeline`
- `GraphicsPipeline`

詳細は [`test/README.md`](test/README.md) を参照してください。

---

## 自分のプロジェクトへの組み込み

### CMake で組み込む場合

サブディレクトリとして追加するなら、利用側の `CMakeLists.txt` で次のようにします。

```cmake
add_subdirectory(path/to/D3D12Helper)

target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

サンプルやテストが不要な場合は、追加前にオプションを OFF にします。

```cmake
set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/D3D12Helper)

target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

### Visual Studio プロジェクトに直接追加する場合

CMake を使わず、既存の Visual Studio プロジェクトへ直接組み込む場合は、次を追加します。

1. インクルードディレクトリに `include/` と `include/D3D12Helper/` を追加する。
2. `src/*.cpp` をプロジェクトのコンパイル対象に追加する。
3. 必要に応じて `dxcompiler.dll` / `dxil.dll` を exe と同じディレクトリへ置く。

このとき、`sample/` と `test/` は追加不要です。

---

## D3D11Helper との対応

D3D12 は D3D11 より明示的に扱う要素が多いため、D3D12Helper では次のような概念をライブラリ側で整理しています。

| D3D12Helper | D3D11Helper 側の対応 | 備考 |
| --- | --- | --- |
| `D3D12Core` | `D3D11Core` | デバイス生成のファサード |
| `D3D12Queue` | Immediate Context | D3D12 では Queue を明示管理 |
| `D3D12CommandContext` | Immediate Context | Command Allocator + Command List |
| `D3D12Fence` | `D3D11Fence` | D3D12 では通常の GPU 同期にも使用 |
| `D3D12Barrier` | ほぼ不要 | D3D12 では Resource State 遷移を明示 |
| `D3D12DescriptorHeap` | View オブジェクト | D3D12 では Descriptor Heap を明示管理 |
| `D3D12UploadBuffer` / `D3D12UploadRing` | `UpdateSubresource` / Map | D3D12 では転送用リソースを明示管理 |
| `D3D12ReadbackBuffer` | Staging Buffer | GPU→CPU の読み戻し |
| `D3D12ComputePipeline` | `D3D11ComputePipeline` | Compute Shader 実行補助 |
| `D3D12GraphicsPipeline` | `D3D11GraphicsPipeline` | D3D12 では PSO と Root Signature を含む |

---

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [`doc/README.md`](doc/README.md) | 全体像・設計思想・ファイル構成・組み込み方法 |
| [`doc/D3D12Core.md`](doc/D3D12Core.md) | Layer 1 API リファレンス |
| [`doc/D3D12Framework.md`](doc/D3D12Framework.md) | Layer 2 API リファレンス |
| [`doc/Patterns.md`](doc/Patterns.md) | よくある処理パターン |
| [`sample/README.md`](sample/README.md) | サンプル一覧と実行方法 |
| [`test/README.md`](test/README.md) | テスト構成と実行方法 |

---

## 注意点

- D3D12 の Resource State は基本的に明示管理です。`D3D12Resource` の状態追跡は簡易的なものであり、サブリソース単位の複雑な状態管理や複数キュー共有では、必要に応じて before / after を手動で指定してください。
- DXC を使う処理では、実行時に `dxcompiler.dll` が必要です。CMake ビルドでは DLL コピー処理を用意していますが、直接プロジェクトへ組み込む場合は自分で配置してください。
- `sample/` と `test/` は利用例・検証用です。ライブラリとして使う場合は `include/` と `src/` を組み込めば十分です。

---

## ライセンス

MIT License
