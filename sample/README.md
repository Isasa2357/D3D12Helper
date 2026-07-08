# D3D12Helper サンプル

D3D12Helper の使い方を示すサンプル集です。`03_HelloTriangle` 以外はウィンドウ不要で、コンソールから実行できます。

| サンプル | 内容 | 主に使う機能 |
| --- | --- | --- |
| [`01_HelloDevice`](01_HelloDevice) | 最小の初期化。アダプタ名・対応機能・キューを表示 | `D3D12Core` / `D3D12CoreConfig` / `D3D12DeviceContext` |
| [`02_ComputeGrayscale`](02_ComputeGrayscale) | 画像を GPU でグレースケール変換し、結果を CPU に読み戻して検証 | `D3D12ComputePipeline` / `ShaderCompiler` / テクスチャ生成 / `DescriptorAllocator` / SRV・UAV / `ReadbackBuffer` / Barrier |
| [`03_HelloTriangle`](03_HelloTriangle) | スワップチェーンでウィンドウに三角形を描く定番サンプル。頂点ごとの色を補間表示 | `CreateSwapChainForHwnd` / `GetSwapChainBackBuffer` / RTV / `D3D12GraphicsPipeline` / `UploadBuffer` / Win32 |
| [`04_ParallelCompute`](04_ParallelCompute) | 1 つの Core を複数スレッドで共有。各スレッドが並列にコマンドリストを記録し、main がまとめて実行 | `std::thread` / スレッドごとの `Context`・`DescriptorAllocator` / 共有 `ComputePipeline`・`Fence` |
| [`05_BufferCompute`](05_BufferCompute) | GPU バッファで SAXPY（`y = a*x + y`）。自前 Root Signature を使う | `CreateBuffer` / `UploadBuffer` + `CopyBufferRegion` / `D3D12ComputePipeline::Initialize` / Root Descriptor / Buffer Readback |
| [`06_UploadRingStreaming`](06_UploadRingStreaming) | 毎フレーム CPU からテクスチャを更新するホットパスを `UploadRing` で回す | `D3D12UploadRing` / `RecordUploadTexture2D`(Ring 版) / `ReclaimCompleted`・`FinishFrame` |
| [`07_ProcessingFusedConvertResize`](07_ProcessingFusedConvertResize) | NV12 → RGBA resize を fused pass で実行し、CPU readback で検証 | `D3D12ProcessingContext` / `D3D12FusedProcessor` / NV12 plane view / readback |
| [`08_ProcessingP010Rgba16`](08_ProcessingP010Rgba16) | P010 / RGBA16F を使う Processing Layer の最小例 | P010 plane view / `R16G16B16A16_FLOAT` / `D3D12FormatConverter` |
| [`18_ProcessingCustomFusedShader`](18_ProcessingCustomFusedShader) | Processing HLSL library を include し、NV12 → RGB → resize → 円形領域外 darken を 1 dispatch に融合 | `D3D12ComputePipeline` / `ProcessingCommon.hlsli` / `ColorSpace.hlsli` / Processing view helpers |

各サンプルの位置づけ:

- **01 → 02** で「初期化 → GPU 実行 → 結果回収」の基本を掴む。
- **03** はウィンドウに三角形を描く（描画ループ・Present・グラフィクス PSO）。GUI が必要。
- **04** はマルチスレッドでの Core 共有。
- **05** はテクスチャではなく**バッファ**に対する計算と、**自前 Root Signature**の作り方。
- **06** は毎フレーム更新の**高 fps アップロード**パターン。
- **07** は Processing Layer の用意済み fused convert+resize ショートカット。
- **08** は P010 と RGBA16F の format 拡張パターン。
- **18** は Processing Layer の HLSL library を使い、アプリ側で独自 fused pass を作るパターン。

API の詳細は [`../doc`](../doc) を、Processing Layer は [`../doc/D3D12Processing.md`](../doc/D3D12Processing.md) と [`../doc/D3D12ProcessingWorkflow.md`](../doc/D3D12ProcessingWorkflow.md) を参照してください。

---

## 必要環境

- Windows 10 / 11
- Direct3D 12 対応 GPU（無くても WARP で動作。サンプル側では基本的に `allowWarpAdapter = true`）
- Visual Studio 2019 以降（MSVC）
- CMake 3.20 以降
- DXC を使うサンプルは実行時に `dxcompiler.dll`（必要に応じて `dxil.dll`）が必要

---

## ビルド

リポジトリルートからビルドします。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Release
```

Visual Studio generator の既定構成では、サンプル実行ファイルは `out/build/default/sample/<Config>/` 以下に生成されます。

生成物例:

```text
out/build/default/sample/Release/D3D12Sample_01_HelloDevice.exe
out/build/default/sample/Release/D3D12Sample_02_ComputeGrayscale.exe
out/build/default/sample/Release/D3D12Sample_03_HelloTriangle.exe
out/build/default/sample/Release/D3D12Sample_04_ParallelCompute.exe
out/build/default/sample/Release/D3D12Sample_05_BufferCompute.exe
out/build/default/sample/Release/D3D12Sample_06_UploadRingStreaming.exe
out/build/default/sample/Release/D3D12Sample_07_ProcessingFusedConvertResize.exe
out/build/default/sample/Release/D3D12Sample_08_ProcessingP010Rgba16.exe
out/build/default/sample/Release/D3D12Sample_18_ProcessingCustomFusedShader.exe
```

構成や generator によって出力パスは変わります。Visual Studio generator の場合は `Debug` / `Release` などの構成別ディレクトリが作られます。

---

## 実行

```bat
out\build\default\sample\Release\D3D12Sample_01_HelloDevice.exe
out\build\default\sample\Release\D3D12Sample_02_ComputeGrayscale.exe
out\build\default\sample\Release\D3D12Sample_07_ProcessingFusedConvertResize.exe
out\build\default\sample\Release\D3D12Sample_08_ProcessingP010Rgba16.exe
out\build\default\sample\Release\D3D12Sample_18_ProcessingCustomFusedShader.exe
```

`RESULT: OK` が出るサンプルは、GPU 実行・読み戻し・検証まで完了しています。

その他のサンプル:

- `D3D12Sample_03_HelloTriangle.exe` … ウィンドウが開き、赤・緑・青の頂点色が補間された三角形が表示されます。ESC か閉じるボタンで終了。GUI 環境が必要です。
- `D3D12Sample_04_ParallelCompute.exe` … 複数スレッドが並列にコマンドを記録し、各スレッドの出力が正しいか検証します。
- `D3D12Sample_05_BufferCompute.exe` … GPU で SAXPY を計算し、CPU 参照と一致するか検証します。
- `D3D12Sample_06_UploadRingStreaming.exe` … 120 フレームぶんのテクスチャ更新をリングで流し、リング使用量を表示します。
- `D3D12Sample_07_ProcessingFusedConvertResize.exe` … NV12 → RGBA resize を 1 dispatch で実行し、CPU readback で検証します。
- `D3D12Sample_08_ProcessingP010Rgba16.exe` … P010 / RGBA16F の Processing API 使用例を検証します。
- `D3D12Sample_18_ProcessingCustomFusedShader.exe` … Processing HLSL library を使ったアプリ側 custom fused shader の例です。

---

## 自分のプロジェクトへの組み込み

CMake を使わない場合も、基本は同じです。

1. インクルードパスに `include/` と `include/D3D12Helper/` を追加する。
2. `src/*.cpp` を自分のビルドに含める。
3. Processing Layer を使う場合は `shaders/D3D12Processing/` を実行時に参照できる場所へ置く。
4. 次のようにインクルードする。

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
#include "D3D12Processing/D3D12Processing.hpp"
```

リンクするシステムライブラリ（`d3d12` / `dxgi` / `dxguid` / `d3dcompiler` / `dxcompiler`）は `D3D12Common.hpp` の `#pragma comment(lib, ...)` で MSVC 向けに自動指定されます。
