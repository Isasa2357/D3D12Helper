# D3D12Helper サンプル

D3D12Helper の使い方を示すサンプル集です。どちらもウィンドウ不要で、コンソールから実行できます。

| サンプル | 内容 | 主に使う機能 |
| --- | --- | --- |
| [`01_HelloDevice`](01_HelloDevice) | 最小の初期化。アダプタ名・対応機能・キューを表示 | `D3D12Core` / `D3D12CoreConfig` / `D3D12DeviceContext` / `D3D12Debug` |
| [`02_ComputeGrayscale`](02_ComputeGrayscale) | 画像を GPU でグレースケール変換し、結果を CPU に読み戻して検証 | `ComputePipeline`(テンプレ RootSig) / `ShaderCompiler`(DXC) / テクスチャ生成 / `DescriptorAllocator` / SRV・UAV / `ReadbackBuffer` / Barrier |
| [`03_HelloTriangle`](03_HelloTriangle) | スワップチェーンでウィンドウに三角形を描く定番サンプル。頂点ごとの色を補間表示 | `CreateSwapChainForHwnd` / `GetSwapChainBackBuffer` / RTV(`DescriptorAllocator`) / `ShaderCompiler`(VS・PS) / `D3D12GraphicsPipeline`+`PipelineDefaults` / `UploadBuffer`(頂点バッファ) / Win32 |
| [`04_ParallelCompute`](04_ParallelCompute) | 1 つの Core を複数スレッドで共有。各スレッドが並列にコマンドリストを記録し、main がまとめて実行 | `std::thread` / スレッドごとの `Context`・`DescriptorAllocator` / 共有 `ComputePipeline`・`Fence` |
| [`05_BufferCompute`](05_BufferCompute) | GPU バッファで SAXPY(y=a*x+y)。自前 Root Signature を使う | `CreateBuffer` / `UploadBuffer` + `CopyBufferRegion` / `ComputePipeline::Initialize`(テンプレ以外) / Root Descriptor / バッファ Readback |
| [`06_UploadRingStreaming`](06_UploadRingStreaming) | 毎フレーム CPU からテクスチャを更新するホットパスを `UploadRing` で回す | `UploadRing` / `RecordUploadTexture2D`(Ring 版) / `ReclaimCompleted`・`FinishFrame` / 複数フレーム在庫 |

各サンプルの位置づけ:

- **01 → 02** で「初期化 → GPU 実行 → 結果回収」の基本を掴む。
- **03** はウィンドウに三角形を描く（描画ループ・Present・グラフィクス PSO）。GUI が必要。
- **04** はマルチスレッドでの Core 共有。
- **05** はテクスチャではなく**バッファ**に対する計算と、**自前 Root Signature**の作り方。
- **06** は毎フレーム更新の**高 fps アップロード**パターン。

API の詳細は [`../doc`](../doc) を、処理パターンの断片は [`../doc/Patterns.md`](../doc/Patterns.md) を参照してください。

---

## 必要環境

- Windows 10 / 11
- Direct3D 12 対応 GPU（無くても WARP で動作。両サンプルとも `allowWarpAdapter = true`）
- Visual Studio 2019 以降（MSVC）
- CMake 3.20 以降
- `02_ComputeGrayscale` は実行時に `dxcompiler.dll`（DXC）が PATH 上に必要

> `dxcompiler.dll` / `dxil.dll` は Windows SDK に含まれます。見つからない場合は
> [DirectXShaderCompiler のリリース](https://github.com/microsoft/DirectXShaderCompiler/releases)
> から入手し、exe と同じフォルダに置くか PATH を通してください。

---

## ビルド

`sample/` ディレクトリで CMake を使います（"x64 Native Tools Command Prompt for VS" などから）。

```bat
cd sample
cmake -S . -B build -A x64
cmake --build build --config Release
```

生成物:

```
sample/build/Release/01_HelloDevice.exe
sample/build/Release/02_ComputeGrayscale.exe
sample/build/Release/03_HelloTriangle.exe
sample/build/Release/04_ParallelCompute.exe
sample/build/Release/05_BufferCompute.exe
sample/build/Release/06_UploadRingStreaming.exe
```

CMake はライブラリ本体（`../include/**/*.cpp`）を static library にまとめてリンクします。事前ビルド済みの `.lib` は不要です。

---

## 実行

```bat
build\Release\01_HelloDevice.exe
build\Release\02_ComputeGrayscale.exe
```

`01_HelloDevice` の出力例:

```
==== D3D12Helper / HelloDevice ====
Adapter      : NVIDIA GeForce RTX ...
Adapter LUID : 0,xxxxx
Resource sharing supported : yes
Typed UAV load (R32_FLOAT) : yes

Queues:
  - Direct (always created)
  - Copy
  - Compute

Device is ready. (no GPU work was submitted)
```

`02_ComputeGrayscale` の出力例:

```
Adapter: NVIDIA GeForce RTX ...
Dispatched 32 x 32 thread groups.
Readback row pitch: 1024 bytes (image width = 1024 bytes)
Max per-channel diff vs CPU reference: 1
RESULT: OK - GPU grayscale matches CPU reference.
```

`RESULT: OK` が出れば、コンパイル → アップロード → Dispatch → リードバックまでの一連が正しく動いています。GPU の輝度計算と CPU 参照実装を全ピクセルで突き合わせ、丸め誤差 ±1 まで許容して検証しています。

その他のサンプル:

- `03_HelloTriangle.exe` … ウィンドウが開き、赤・緑・青の頂点色が補間された三角形が表示されます。ESC か閉じるボタンで終了。GUI 環境が必要です。
- `04_ParallelCompute.exe` … 4 スレッドが並列にコマンドを記録し、各スレッドの出力が正しいか検証します（`thread N : ... [OK]`）。
- `05_BufferCompute.exe` … GPU で SAXPY を計算し、CPU 参照と一致するか検証します。
- `06_UploadRingStreaming.exe` … 120 フレームぶんのテクスチャ更新をリングで流し、リング使用量を表示します。

---

## 自分のプロジェクトへの組み込み

CMake を使わない場合も、やることは同じです。

1. インクルードパスに `include/` を追加する。
2. `include/D3D12Core/*.cpp` と `include/D3D12Framework/*.cpp` を自分のビルドに含める。
3. 次のようにインクルードする。

```cpp
#include "D3D12Core/D3D12Core.hpp"          // Layer 1 ファサード
#include "D3D12Framework/D3D12Framework.hpp" // Layer 2 まとめ include
```

リンクするシステムライブラリ（`d3d12` / `dxgi` / `dxguid` / `d3dcompiler` / `dxcompiler`）は
`D3D12Common.hpp` の `#pragma comment(lib, ...)` で MSVC 向けに自動指定されます。
