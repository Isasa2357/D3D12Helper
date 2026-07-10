# D3D12Helper

Direct3D 12 の定型処理を薄くラップした **C++17 ヘルパライブラリ**です。

**現在の安定版は v1.12.0 です。** v1.12.0 では、D3D12Processing に affine / perspective transform、3D LUT application、undistort-map application facade を追加しました。既存の `D3D12Framework` は v1.x 互換 wrapper として維持します。

デバイス生成、DXGI アダプタ選択、Queue / Fence / CommandList 管理、リソース作成、Descriptor 管理、Upload / Readback、Compute / Graphics パイプライン、シェーダコンパイル、SwapChain 作成、共有リソース作成、GPU 画像処理など、D3D12 アプリケーションで繰り返し書くコードを軽量クラスと自由関数にまとめています。

> 姉妹プロジェクト [D3D11Helper](https://github.com/Isasa2357/D3D11Helper) と近い設計思想で、D3D11 / D3D12 間の移植時に認知コストが小さくなるようにしています。

---

## 特徴

- **公開モジュール分離**  
  `D3D12Foundation`、`D3D12Core`、`D3D12Gpu`、`D3D12Presentation`、`D3D12Processing`、`D3D12Interop`、`D3D12Diagnostics` に分類。
- **`D3D12Core&` を取る自由関数**  
  `CreateBuffer`、`CreateTexture2D`、`CreateStructuredBuffer`、`CreateConstantBuffer`、`CreateSharedTexture2D`、各種 View 作成、`RecordUploadTexture2D` などを 1 行で呼べる。
- **Command Queue / Fence / CommandContext 管理**  
  Direct / Copy / Compute Queue と Fence 値管理、Command Allocator + Command List の Reset / Close を整理。
- **Descriptor 管理**  
  `D3D12DescriptorHeap` と `D3D12DescriptorAllocator` により、RTV / DSV / CBV_SRV_UAV / Sampler Heap の確保を簡略化。
- **Upload / Readback 補助**  
  `D3D12UploadBuffer`、`D3D12UploadRing`、`D3D12ReadbackBuffer` により、CPU↔GPU 転送を扱いやすくする。
- **詳細 Resource 生成 / 検証**  
  `D3D12BufferCreateDesc`、`D3D12Texture2DCreateDesc` による committed resource の詳細指定と、`D3D12Texture2DRequirement` による用途非依存の入力検証を提供。v1.12.1 以前の簡易 API 名とシグネチャは維持する。
- **ComputePipeline / GraphicsPipeline**  
  Root Signature / PSO / Shader の設定をまとめて管理。`Bind` / `Dispatch` などの薄い補助も提供。
- **ShaderCompiler**  
  `.cso` 読み込み、`D3DCompile`、DXC による HLSL コンパイルを扱う。
- **Shared Resource 補助**  
  D3D12 共有ハンドルを作るための `D3D12SharedResource` と、共有可能 Texture2D を作る `CreateSharedTexture2D` を提供。
- **D3D12Processing**  
  GPU 上で format conversion、resize、remap、composite、blur、region effect、mask、threshold、pyramid blur、affine / perspective transform、3D LUT などを実行する Layer 3。NV12 / P010 / RGBA8 / BGRA8 / RGBA16F を扱う。
- **ThrowIfFailed**  
  HRESULT 失敗時に式・ファイル・行・メッセージ付きで例外化。

---

## アーキテクチャ

v1.1.0 以降の公開モジュール構成は次のとおりです。

```text
D3D12Foundation
  DirectX / DXGI / HRESULT / format utility

D3D12Core
  device / adapter / queue / fence / command context / barrier

D3D12Gpu
  resource / descriptor / upload / readback / pipeline / shader compiler / view helpers

D3D12Presentation
  swap-chain and presentation helpers

D3D12Processing
  GPU image processing

D3D12Interop
  shared resource and shared fence helpers

D3D12Diagnostics
  debug layer / InfoQueue / DRED / object naming
```

既存の Layer 対応は次のように維持します。

```text
Layer 1 : D3D12Core
Layer 2 : D3D12Framework  -> v1.1.0 以降は D3D12Gpu / D3D12Presentation / D3D12Interop へ分類
Layer 3 : D3D12Processing
```

`D3D12Framework` は削除せず、v1.x 互換 wrapper として残します。

---

## ディレクトリ構成

```text
D3D12Helper/
├── CMakeLists.txt
├── cmake/
├── include/D3D12Helper/
│   ├── D3D12Foundation/
│   ├── D3D12Core/
│   ├── D3D12Gpu/
│   ├── D3D12Framework/
│   ├── D3D12Presentation/
│   ├── D3D12Processing/
│   ├── D3D12Interop/
│   └── D3D12Diagnostics/
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
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

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

推奨 include 形式:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```

互換 include 形式:

```cpp
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
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
| `D3D12AdvancedProcessor` | affine / perspective transform、3D LUT、undistort-map application facade |

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

NuGet から取得する場合は、リポジトリ直下で次を実行します。

```bat
nuget install Microsoft.Direct3D.DXC -Version 1.9.2602.24 -OutputDirectory packages
```

CMake は `packages/` やユーザーの NuGet キャッシュから `dxcompiler.dll` / `dxil.dll` を探し、サンプル・テストの exe 横へコピーします。

### CMake ビルド

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Debug
```

### テスト

```bat
ctest --test-dir out/build/default -C Debug --output-on-failure
```

特定の suite だけ実行する場合：

```bat
ctest --test-dir out/build/default -C Debug -R Processing --output-on-failure
```
