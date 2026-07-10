# D3D12Helper

Direct3D 12 の定型処理を薄くラップした **C++17 ヘルパライブラリ**です。

**現在の安定版は v1.13.0 です。** v1.13.0 では、範囲指定Readback、Queue Sync Point、詳細Resource生成・検証、非所有`D3D12ResourceView`、typed command-list基盤、NV12/P010対応YUV HLSL primitiveを追加しました。既存のv1.x公開APIと`D3D12Framework`互換wrapperは維持しています。

デバイス生成、DXGIアダプタ選択、Queue / Fence / Command List管理、Resource・Descriptor管理、Upload / Readback、Compute / Graphics pipeline、Shader compile、SwapChain、共有Resource、GPU画像処理など、D3D12アプリケーションで繰り返し書く処理を軽量クラスと自由関数にまとめています。

> 姉妹プロジェクト [D3D11Helper](https://github.com/Isasa2357/D3D11Helper) と近い設計思想で、D3D11 / D3D12間の移植時に認知コストが小さくなるようにしています。

---

## 特徴

- **公開モジュール分離**  
  `D3D12Foundation`、`D3D12Core`、`D3D12Gpu`、`D3D12Presentation`、`D3D12Processing`、`D3D12Interop`、`D3D12Diagnostics`に分類。
- **`D3D12Core&`を取る自由関数**  
  Buffer / Texture / View / Upload / Readback / shared Resourceなどを短い呼び出しで生成・記録。
- **Queue / Fence / Command Context**  
  Direct / Copy / Compute Queue、Fence値、Command Allocator + Command ListのReset / Closeを整理。
- **Queue Sync Point**  
  Signal済みFenceと値を`D3D12QueueSyncPoint`へまとめ、CPU wait・Queue間GPU waitに再利用。
- **Typed Command List**  
  `D3D12CommandAllocatorContext`と`CreateTypedCommandList<T>()`により、Direct / Compute / CopyおよびSDK提供の特殊Command List interfaceを共通生成。
- **詳細Resource生成と検証**  
  heap property、alignment、flags、initial state、clear valueを指定できる詳細APIと、Texture2D要件検証を提供。
- **非所有Resource View**  
  `D3D12ResourceView`は`AddRef` / `Release`を行わず、外部所有Resourceを明示stateでProcessingへ渡せる。
- **Barrier集約**  
  `D3D12BarrierBatch`でTransition / UAV / Aliasing barrierをまとめて保持。
- **Upload / Readback**  
  `D3D12UploadBuffer`、`D3D12UploadRing`、`D3D12ReadbackBuffer`、範囲指定RAII `MapRead()`を提供。
- **Compute / Graphics Pipeline**  
  Root Signature / PSO / Shader設定をまとめ、`Bind` / `Dispatch`などの薄い補助を提供。
- **ShaderCompiler**  
  `.cso`読込、`D3DCompile`、DXCによるHLSL compileを扱う。
- **D3D12Processing**  
  format conversion、resize、remap、composite、blur、region effect、mask、threshold、pyramid、affine / perspective、3D LUTなどをGPU実行。
- **YUV HLSL library**  
  application-owned shaderからincludeできる`YuvPrimitives.hlsli`を提供。NV12 / P010、BT.601 / 709 / 2020、Full / Limited Range、Point / Linear samplingに対応。
- **Shared Resource / Diagnostics**  
  共有Handle、共有Fence、Debug Layer、InfoQueue、DRED、object nameを補助。

---

## アーキテクチャ

```text
D3D12Foundation
  DirectX / DXGI / HRESULT / format utility

D3D12Core
  device / adapter / queue / fence / command allocator / command list / barrier

D3D12Gpu
  resource / descriptor / upload / readback / pipeline / shader compiler / validation

D3D12Presentation
  swap-chain and presentation helpers

D3D12Processing
  GPU image processing and reusable HLSL primitives

D3D12Interop
  shared resource and shared fence helpers

D3D12Diagnostics
  debug layer / InfoQueue / DRED / object naming
```

既存Layerとの対応:

```text
Layer 1 : D3D12Core
Layer 2 : D3D12Framework -> D3D12Gpu / D3D12Presentation / D3D12Interopへ分類
Layer 3 : D3D12Processing
```

`D3D12Framework`は削除せず、v1.x互換wrapperとして残します。

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

Processing Layerを使う場合は`shaders/D3D12Processing/`も実行時に参照できる場所へ配置してください。

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
        config.allowWarpAdapter = true;

        auto core = D3D12Core::CreateShared(config);

        auto buffer = CreateBuffer(
            *core,
            1024,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_NONE);

        std::wcout << L"Adapter: "
                   << core->DeviceContext().GetAdapterName() << L"\n";

        core->WaitIdle();
    } catch (const std::exception& e) {
        std::cerr << "D3D12Helper error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

推奨include:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```

互換include:

```cpp
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
```

CMake target `D3D12Helper::D3D12Helper`をlinkすると必要なinclude pathが設定されます。

---

## v1.13.0 汎用GPU基盤の例

### 範囲指定Readback

```cpp
auto mapped = readback.MapRead(offset, size);
const std::byte* data = mapped.Data();
// mappedの破棄時にread-only rangeとしてUnmapされる。
```

既存の`Map()` / `Unmap()`も互換APIとして残っています。

### Queue Sync Point

```cpp
D3D12QueueSyncPoint point = producerQueue.SignalPoint();
consumerQueue.GpuWaitPoint(point);
point.CpuWait();
```

### Typed Command List

```cpp
D3D12CommandAllocatorContext allocator;
allocator.Initialize(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);

auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
    device,
    D3D12_COMMAND_LIST_TYPE_COMPUTE,
    allocator);
```

AllocatorをResetする前に、そのAllocatorで記録したGPU処理がFence等で完了していることを呼び出し側が保証してください。

### 非所有Resource View

```cpp
D3D12ProcessingStateDesc state;
state.useExplicitStates = true;
state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
state.srcAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
state.dstAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;

converter.RecordConvertView(
    commandContext,
    D3D12ResourceView(externalSrc),
    D3D12ResourceView(externalDst),
    desc,
    state);
```

ViewはResourceを所有しません。投入したGPU処理が完了するまで外部ownerがResourceを保持してください。

### Custom fused YUV shader

```hlsl
#include "ProcessingCommon.hlsli"
#include "YuvPrimitives.hlsli"

float3 rgb = D3D12SampleYuv420RgbLinear(
    YPlane,
    UVPlane,
    destinationPixel,
    uint2(SrcX, SrcY),
    uint2(SrcWidth, SrcHeight),
    float2(ScaleX, ScaleY),
    SrcFormat,
    SrcRange,
    SrcMatrix);
```

変換・resize・独自effectを単一dispatchへ融合できます。

---

## Processing Layer の最小例

```cpp
using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

D3D12DescriptorAllocator cbvSrvUav;
cbvSrvUav.Initialize(
    core->GetDevice(),
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    1024,
    true);

D3D12DescriptorAllocator sampler;
sampler.Initialize(
    core->GetDevice(),
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
    16,
    true);

D3D12ProcessingContext processing;
processing.Initialize(
    *core,
    &cbvSrvUav,
    &sampler,
    "shaders/D3D12Processing");

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

詳細は[`doc/D3D12Processing.md`](doc/D3D12Processing.md)と[`doc/D3D12ProcessingWorkflow.md`](doc/D3D12ProcessingWorkflow.md)を参照してください。

---

## Processing API 一覧

| Processor | 主な機能 |
| --- | --- |
| `D3D12FormatConverter` | RGBA-like↔RGBA-like、NV12/P010↔RGBA-like |
| `D3D12Resizer` | RGBA-like Point / Linear resize |
| `D3D12Remapper` | R32G32_FLOAT mapによるremap |
| `D3D12Compositor` | Copy / AlphaBlend / PremultipliedAlpha / Add |
| `D3D12FusedProcessor` | convert + resize fused pass |
| `D3D12Blurrer` | Gaussian / Box blur |
| `D3D12RegionEffectProcessor` | 円形・矩形region effect |
| `D3D12RegionBlur` | 領域内・領域外blur |
| `D3D12ColorAdjuster` | brightness / contrast / gamma / saturation |
| `D3D12KernelFilter` | Custom3x3 / Sharpen / EdgeDetect |
| `D3D12MaskProcessor` | apply / blend / combine / invert mask |
| `D3D12ThresholdProcessor` | threshold / heatmap / class color map / overlay |
| `D3D12PyramidProcessor` | downsample2x / upsample2x |
| `D3D12PyramidBlur` | downsample → low-res blur → upsample |
| `D3D12PyramidRegionBlur` | 高速region blur |
| `D3D12AdvancedProcessor` | affine / perspective / 3D LUT / undistort map |

呼び出し側Resourceを受け取る公開`Record*`処理には、明示state専用の別名`*View`経路があります。

---

## ビルド

### 必要環境

- Windows 10 / 11
- Visual Studio 2019以降
- MSVC / C++17
- CMake 3.20以降
- Direct3D 12対応GPU、またはWARP
- DXCを使う場合は`dxcompiler.dll` / `dxil.dll`

NuGetからDXC runtimeを取得する例:

```bat
nuget install Microsoft.Direct3D.DXC -Version 1.9.2602.24 -OutputDirectory packages
```

CMake build:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Debug --parallel
ctest --test-dir out/build/default -C Debug --parallel --output-on-failure
```

Install / `find_package` smoke testを含むrelease確認では、`D3D12HELPER_INSTALL=ON`と`D3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS=ON`を指定してください。

---

## サンプル

| 名前 | 概要 | ウィンドウ |
| --- | --- | --- |
| `01_HelloDevice` | Device・Adapter・Queue情報 | 不要 |
| `02_ComputeGrayscale` | Compute shader + CPU readback | 不要 |
| `03_HelloTriangle` | Win32 + Graphics pipeline | あり |
| `04_ParallelCompute` | 複数threadでcommand記録 | 不要 |
| `05_BufferCompute` | Structured buffer SAXPY | 不要 |
| `06_UploadRingStreaming` | UploadRingによる連続更新 | 不要 |
| `07_ProcessingFusedConvertResize` | NV12 → RGBA fused resize | 不要 |
| `08_ProcessingP010Rgba16` | P010 / RGBA16F | 不要 |
| `09_ProcessingBlur` | Blur | 不要 |
| `10_ProcessingRegionEffect` | Region effect | 不要 |
| `11_ProcessingRegionBlur` | Region blur | 不要 |
| `12_ProcessingColorAdjust` | Color adjustment | 不要 |
| `13_ProcessingKernelFilter` | 3x3 kernel filter | 不要 |
| `14_ProcessingMask` | Mask処理 | 不要 |
| `15_ProcessingThreshold` | Threshold / heatmap | 不要 |
| `16_ProcessingPyramid` | Downsample / Upsample | 不要 |
| `17_ProcessingPyramidBlur` | Pyramid blur / region blur | 不要 |
| `18_ProcessingCustomFusedShader` | YUV primitiveを使う独自fused shader | 不要 |
| `19_TypedCommandList` | Typed allocator / command list | 不要 |

詳細は[`sample/README.md`](sample/README.md)を参照してください。

---

## テスト

`test/`は外部test frameworkに依存しない軽量test executableをCTest suite単位で実行します。

主なsuite:

```text
ModuleHeaders
Core
Fence
CommandContext
TypedCommandList
ResourceCreateValidation
ResourceView
UploadReadback
ShaderCompiler
ComputePipeline
GraphicsPipeline
Processing
YuvHlslPrimitives
ProcessingBlur
ProcessingRegionEffect
ProcessingRegionBlur
ProcessingColorAdjust
ProcessingKernelFilter
ProcessingMask
ProcessingThreshold
ProcessingPyramid
ProcessingPyramidBlur
ProcessingPyramidRegionBlur
AdvancedProcessing
CompatibilityV1121
CoverageHardening
```

全一覧と対象は[`doc/TestCoverage.md`](doc/TestCoverage.md)を参照してください。

---

## 自分のプロジェクトへの組み込み

### CMake

```cmake
set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

### Visual Studioへ直接追加

1. include directoryに`include/`と`include/D3D12Helper/`を追加する。
2. `src/*.cpp`をcompile対象に追加する。
3. Processing Layerを使う場合は`shaders/D3D12Processing/`を配置する。
4. 必要に応じて`dxcompiler.dll` / `dxil.dll`をexe横へ置く。

---

## D3D11Helperとの対応

| D3D12Helper | D3D11Helper側 | 備考 |
| --- | --- | --- |
| `D3D12Foundation` | `D3D11Foundation` | DirectX / DXGI / HRESULT utility |
| `D3D12Core` | `D3D11Core` | Device facade |
| `D3D12Queue` | Immediate Context | D3D12ではQueueを明示管理 |
| `D3D12CommandContext` | Immediate Context | Allocator + Command List |
| `D3D12Fence` | `D3D11Fence` | GPU同期 |
| `D3D12Gpu` | `D3D11Gpu` | Resource / Descriptor / Transfer / Pipeline |
| `D3D12Presentation` | `D3D11Presentation` | SwapChain / Present |
| `D3D12Processing` | `D3D11Processing` | GPU画像処理 |
| `D3D12Interop` | `D3D11Interop` | Shared Resource / Fence |
| `D3D12Diagnostics` | `D3D11Diagnostics` | Debug / InfoQueue / DRED |
| `D3D12Framework` | v1.x compatibility wrapper | 既存利用者向け |

---

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [`doc/README.md`](doc/README.md) | ドキュメントindexと設計思想 |
| [`doc/Architecture.md`](doc/Architecture.md) | 公開module構成 |
| [`doc/D3D12Core.md`](doc/D3D12Core.md) | Core API |
| [`doc/D3D12Gpu.md`](doc/D3D12Gpu.md) | GPU API |
| [`doc/D3D12Processing.md`](doc/D3D12Processing.md) | Processing API |
| [`doc/D3D12ProcessingWorkflow.md`](doc/D3D12ProcessingWorkflow.md) | Custom fused HLSL workflow |
| [`doc/GenericGpuFoundationPhase1.md`](doc/GenericGpuFoundationPhase1.md) | Phase 1設計 |
| [`doc/GenericGpuFoundationPhase2Audit.md`](doc/GenericGpuFoundationPhase2Audit.md) | 非所有Resource監査 |
| [`doc/GenericGpuFoundationPhase3TypedCommandList.md`](doc/GenericGpuFoundationPhase3TypedCommandList.md) | Typed Command List設計 |
| [`doc/GenericGpuFoundationPhase4YuvHlslPrimitives.md`](doc/GenericGpuFoundationPhase4YuvHlslPrimitives.md) | YUV primitive設計 |
| [`doc/GenericGpuFoundationFinalAudit.md`](doc/GenericGpuFoundationFinalAudit.md) | Phase 1～4最終監査 |
| [`doc/ReleaseNotes_v1.13.0.md`](doc/ReleaseNotes_v1.13.0.md) | v1.13.0 release notes |
| [`doc/TestCoverage.md`](doc/TestCoverage.md) | CTest coverage |
| [`sample/README.md`](sample/README.md) | サンプル一覧 |
| [`test/README.md`](test/README.md) | test構成 |

---

## 注意点

- Resource Stateは基本的に明示管理です。`D3D12Resource`の追跡はResource全体に対する簡易stateであり、subresource単位、複数Queue、外部API共有ではbefore / afterを明示してください。
- `D3D12ResourceView`は非所有です。GPU処理完了までResource lifetimeを呼び出し側が保証してください。
- Command AllocatorのReset前に、関連するGPU処理の完了をFenceで保証してください。
- DXCを使う処理では実行時に`dxcompiler.dll`が必要です。
- Processing Layerでは`shaders/D3D12Processing/`を実行時に参照できるようにしてください。
- `sample/`と`test/`は利用例・検証用です。組み込み時は`include/`、`src/`、必要に応じて`shaders/`を使用します。
