# D3D12Helper

Direct3D 12 の定型処理を薄くラップした **Layer 1 / Layer 2 / Layer 3 構成の C++17 ヘルパライブラリ**です。

**現在の安定版は v1.0.0 です。** v1.0.0 は、既存の Core / Framework / Processing / test / sample を安定版として固定するリリースです。以降の v1.1.0 では、D3D11Helper と同じ Foundation / Core / Gpu / Presentation / Processing / Interop / Diagnostics 体系への再整理を進めます。

デバイス生成、DXGI アダプタ選択、Queue / Fence / CommandList 管理、リソース作成、Descriptor 管理、Upload / Readback、Compute / Graphics パイプライン、シェーダコンパイル、SwapChain 作成、共有リソース作成、GPU 画像処理など、D3D12 アプリケーションで繰り返し書くコードを軽量クラスと自由関数にまとめています。

> 姉妹プロジェクト [D3D11Helper](https://github.com/Isasa2357/D3D11Helper) と近い設計思想で、D3D11 / D3D12 間の移植時に認知コストが小さくなるようにしています。

---

## 特徴

- **3 レイヤー分離**  
  Layer 1（Core: Device / Queue / Fence / CommandContext / DXGI）、Layer 2（Framework: Resource / Descriptor / Upload / Pipeline）、Layer 3（Processing: GPU 画像処理）を分離。
- **`D3D12Core&` を取る自由関数**  
  `CreateBuffer`、`CreateTexture2D`、`CreateStructuredBuffer`、`CreateConstantBuffer`、`CreateSharedTexture2D`、各種 View 作成、`RecordUploadTexture2D` などを 1 行で呼べる。
- **Command Queue / Fence / CommandContext 管理**  
  Direct / Copy / Compute Queue と Fence 値管理、Command Allocator + Command List の Reset / Close を整理。
- **Descriptor 管理**  
  `D3D12DescriptorHeap` と `D3D12DescriptorAllocator` により、RTV / DSV / CBV_SRV_UAV / Sampler Heap の確保を簡略化。
- **Upload / Readback 補助**  
  `D3D12UploadBuffer`、`D3D12UploadRing`、`D3D12ReadbackBuffer` により、CPU↔GPU 転送を扱いやすくする。
- **ComputePipeline / GraphicsPipeline**  
  Root Signature / PSO / Shader の設定をまとめて管理。`Bind` / `Dispatch` などの薄い補助も提供。
- **ShaderCompiler**  
  `.cso` 読み込み、`D3DCompile`、DXC による HLSL コンパイルを扱う。
- **Shared Resource 補助**  
  D3D12 共有ハンドルを作るための `D3D12SharedResource` と、共有可能 Texture2D を作る `CreateSharedTexture2D` を提供。
- **D3D12Processing**  
  GPU 上で format conversion、resize、remap、composite、blur、region effect、mask、threshold、pyramid blur などを実行する Layer 3。NV12 / P010 / RGBA8 / BGRA8 / RGBA16F を扱う。
- **ThrowIfFailed**  
  HRESULT 失敗時に式・ファイル・行・メッセージ付きで例外化。

---

## アーキテクチャ

```text
+--------------------------------------------------------------+
|  アプリ / 上位サブシステム                                   |
|  Window / Camera / Renderer / Processing / Interop など      |
+--------------------------------------------------------------+
                    | shared_ptr<D3D12Core> を共有
                    v
+--------------------------------------------------------------+
|  Layer 3 : D3D12Processing                                   |
|    GPU image processing                                      |
|      FormatConvert / Resize / Remap / Composite              |
|      Blur / RegionEffect / RegionBlur                        |
|      ColorAdjust / KernelFilter                              |
|      Mask / Threshold / Pyramid / PyramidBlur                |
|      HLSL shader library                                     |
+--------------------------------------------------------------+
                    |
                    v
+--------------------------------------------------------------+
|  Layer 2 : D3D12Framework                                    |
|    Resource / Descriptor / Upload / Readback / Pipeline      |
+--------------------------------------------------------------+
                    |
                    v
+--------------------------------------------------------------+
|  Layer 1 : D3D12Core                                         |
|    Device / Queue / Fence / CommandContext / Barrier         |
+--------------------------------------------------------------+
```

---

## ディレクトリ構成

```text
D3D12Helper/
├── CMakeLists.txt
├── cmake/
├── include/D3D12Helper/
│   ├── D3D12Core/
│   ├── D3D12Framework/
│   └── D3D12Processing/
├── shaders/D3D12Processing/
├── src/
├── sample/
├── test/
└── doc/
```

Processing Layer を使う場合は `shaders/D3D12Processing/` も実行時に参照できる場所へ配置してください。

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
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_NONE);

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
#include "D3D12Processing/D3D12Processing.hpp"
```

CMake ターゲット `D3D12Helper::D3D12Helper` をリンクすると、どちらの include 形式も使えるように include path を設定しています。

---

## Processing Layer の最小例

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
#include "D3D12Processing/D3D12Processing.hpp"

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

D3D12DescriptorAllocator cbvSrvUav;
cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true);

D3D12DescriptorAllocator sampler;
sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);

D3D12ProcessingContext processing;
processing.Initialize(*core, &cbvSrvUav, &sampler, "shaders/D3D12Processing");

D3D12FusedProcessor fused;
fused.Initialize(processing);

auto dst = fused.CreateOutputTexture(
    *core,
    1280,
    720,
    DXGI_FORMAT_R8G8B8A8_UNORM);

FusedConvertResizeDesc desc;
desc.srcFormat = DXGI_FORMAT_NV12;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.filter = ProcessingFilter::Linear;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
fused.RecordConvertResize(ctx, srcNv12, dst, desc);
ctx.Close();
```

詳細は [`doc/D3D12Processing.md`](doc/D3D12Processing.md) を参照してください。

---

## Processing Layer の機能一覧

| Processor | 主な機能 |
| --- | --- |
| `D3D12FormatConverter` | RGBA-like↔RGBA-like、NV12/P010→RGBA-like、RGBA-like→NV12/P010 |
| `D3D12Resizer` | RGBA-like resize。Point / Linear |
| `D3D12Remapper` | R32G32_FLOAT map による remap |
| `D3D12Compositor` | Copy / AlphaBlend / PremultipliedAlpha / Add |
| `D3D12FusedProcessor` | convert + resize fused pass |
| `D3D12Blurrer` | Gaussian / Box blur |
| `D3D12RegionEffectProcessor` | 円形・矩形領域の darken / tint / grayscale / highlight / alpha fade / vignette |
| `D3D12RegionBlur` | 領域内・領域外 blur |
| `D3D12ColorAdjuster` | brightness / contrast / gamma / saturation |
| `D3D12KernelFilter` | Custom3x3 / Sharpen / EdgeDetect |
| `D3D12MaskProcessor` | apply mask / blend by mask / combine masks / invert |
| `D3D12ThresholdProcessor` | threshold / range threshold / heatmap / class color map / mask overlay |
| `D3D12PyramidProcessor` | downsample2x / upsample2x |
| `D3D12PyramidBlur` | downsample → low-res blur → upsample |
| `D3D12PyramidRegionBlur` | 高速 region blur |

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

DXC を使うサンプルやテストでは `dxcompiler.dll` と `dxil.dll` が実行ファイルと同じディレクトリ、または `PATH` 上に必要です。

Visual Studio / Windows SDK に含まれる DXC を使う場合は、CMake が代表的なインストール先を探索し、見つかった DLL をテストやサンプルの実行ディレクトリへコピーします。

### CMake

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64
cmake --build out/build/default --config Debug
ctest --test-dir out/build/default -C Debug --output-on-failure
```

### オプション

```bat
cmake -S . -B out/build/default ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON
```

---

## サンプル

`sample/` 以下に小さな確認用サンプルがあります。

| sample | 内容 |
| --- | --- |
| `01_HelloDevice` | Device / Queue 初期化と adapter 情報表示 |
| `02_BufferUploadReadback` | Buffer upload / readback |
| `03_TextureUploadReadback` | Texture2D upload / readback |
| `04_ComputeBufferAdd` | Compute shader による buffer 加算 |
| `05_WindowTriangle` | Window / SwapChain / GraphicsPipeline |
| `06_SharedTexture` | 共有可能 Texture2D 作成 |
| `07_ProcessingResize` | GPU resize |
| `08_ProcessingNv12ToRgba` | NV12 → RGBA 変換 |
| `09_ProcessingBlur` | blur |
| `10_ProcessingRegionEffect` | region effect |
| `11_ProcessingRegionBlur` | region blur |
| `12_ProcessingColorAdjust` | color adjust |
| `13_ProcessingKernelFilter` | 3x3 kernel filter |
| `14_ProcessingMask` | mask processing |
| `15_ProcessingThreshold` | threshold / heatmap / class color map |
| `16_ProcessingPyramid` | downsample / upsample |
| `17_ProcessingPyramidBlur` | pyramid blur / pyramid region blur |

---

## テスト

CTest で以下の領域を確認します。

- FormatUtil / DxgiUtil / ThrowIfFailed
- Core / Queue / Fence / CommandContext
- Descriptor / Resource / Upload / Readback
- ComputePipeline / GraphicsPipeline / ShaderCompiler
- Processing 系各機能

```bat
ctest --test-dir out/build/default -C Debug --output-on-failure
```

---

## 設計メモ

- ライブラリ本体は DirectX / DXGI / DXC に閉じます。
- PNG / JPEG / MP4 / NVENC / Media Foundation / OpenCV などの file/media I/O は含めません。
- D3D12 の同期・Barrier・Descriptor は D3D11 より明示的なため、D3D11Helper と API 名を完全一致させるより、機能カテゴリを揃えることを優先します。
- `D3D12Framework` は v1.x 互換 wrapper として維持し、今後の v1.1.0 以降で `D3D12Gpu` / `D3D12Presentation` / `D3D12Interop` / `D3D12Diagnostics` へ段階的に整理します。

---

## ライセンス

このリポジトリのライセンス設定に従います。
