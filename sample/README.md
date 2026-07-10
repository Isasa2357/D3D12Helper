# D3D12Helper サンプル

D3D12Helperの主要機能を個別に確認できるサンプル集です。`03_HelloTriangle`以外はウィンドウ不要で、コンソールから実行できます。

| サンプル | 内容 | 主に使う機能 |
| --- | --- | --- |
| [`01_HelloDevice`](01_HelloDevice) | Device初期化、Adapter・Queue情報表示 | `D3D12Core` / `D3D12CoreConfig` |
| [`02_ComputeGrayscale`](02_ComputeGrayscale) | Compute grayscaleとCPU readback | `D3D12ComputePipeline` / SRV / UAV / Readback |
| [`03_HelloTriangle`](03_HelloTriangle) | Win32 windowへ三角形描画 | SwapChain / RTV / `D3D12GraphicsPipeline` |
| [`04_ParallelCompute`](04_ParallelCompute) | 複数threadでcommandを並列記録 | thread別Context / Descriptor / Fence |
| [`05_BufferCompute`](05_BufferCompute) | Structured BufferによるSAXPY | Buffer / Root Descriptor / Readback |
| [`06_UploadRingStreaming`](06_UploadRingStreaming) | 毎frame texture更新 | `D3D12UploadRing` / transfer helper |
| [`07_ProcessingFusedConvertResize`](07_ProcessingFusedConvertResize) | NV12→RGBA resizeを1 dispatchで実行 | `D3D12FusedProcessor` / YUV plane view |
| [`08_ProcessingP010Rgba16`](08_ProcessingP010Rgba16) | P010 / RGBA16F Processing | P010 plane / `D3D12FormatConverter` |
| [`09_ProcessingBlur`](09_ProcessingBlur) | Gaussian / Box blur | `D3D12Blurrer` |
| [`10_ProcessingRegionEffect`](10_ProcessingRegionEffect) | 円形・矩形region effect | `D3D12RegionEffectProcessor` |
| [`11_ProcessingRegionBlur`](11_ProcessingRegionBlur) | 領域内・領域外blur | `D3D12RegionBlur` |
| [`12_ProcessingColorAdjust`](12_ProcessingColorAdjust) | brightness / contrast / gamma / saturation | `D3D12ColorAdjuster` |
| [`13_ProcessingKernelFilter`](13_ProcessingKernelFilter) | 3x3 filter | `D3D12KernelFilter` |
| [`14_ProcessingMask`](14_ProcessingMask) | apply / blend / combine / invert mask | `D3D12MaskProcessor` |
| [`15_ProcessingThreshold`](15_ProcessingThreshold) | threshold / heatmap / color map | `D3D12ThresholdProcessor` |
| [`16_ProcessingPyramid`](16_ProcessingPyramid) | Downsample2x / Upsample2x | `D3D12PyramidProcessor` |
| [`17_ProcessingPyramidBlur`](17_ProcessingPyramidBlur) | Pyramid blur / region blur | `D3D12PyramidBlur` / `D3D12PyramidRegionBlur` |
| [`18_ProcessingCustomFusedShader`](18_ProcessingCustomFusedShader) | YUV→RGB→resize→独自effectを1 dispatchへ融合 | `YuvPrimitives.hlsli` / `D3D12ComputePipeline` |
| [`19_TypedCommandList`](19_TypedCommandList) | typed allocatorとcommand list生成 | `D3D12CommandAllocatorContext` / `CreateTypedCommandList<T>` |

## 学習順の目安

- **01→02**: 初期化、GPU command、結果回収の基本。
- **03**: 描画loop、Present、Graphics PSO。
- **04**: Core共有と並列command記録。
- **05**: Buffer計算と独自Root Signature。
- **06**: 高fpsの連続Upload。
- **07～17**: 用意済みProcessing pass。
- **18**: HLSL primitiveをincludeして、アプリ側で単一dispatchへ処理を融合。
- **19**: Command Allocator所有とCommand List interface選択の分離。

APIの詳細は[`../doc`](../doc)を参照してください。Processing Layerは[`../doc/D3D12Processing.md`](../doc/D3D12Processing.md)、custom fused shaderは[`../doc/D3D12ProcessingWorkflow.md`](../doc/D3D12ProcessingWorkflow.md)、typed command listは[`../doc/GenericGpuFoundationPhase3TypedCommandList.md`](../doc/GenericGpuFoundationPhase3TypedCommandList.md)に記載しています。

---

## 必要環境

- Windows 10 / 11
- Direct3D 12対応GPU、またはWARP
- Visual Studio 2019以降
- CMake 3.20以降
- DXC利用時は`dxcompiler.dll`、必要に応じて`dxil.dll`

---

## ビルド

リポジトリrootで実行します。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Release --parallel
```

Visual Studio generatorでは、実行ファイルは`out/build/default/sample/<Config>/`に生成されます。

```text
out/build/default/sample/Release/D3D12Sample_01_HelloDevice.exe
out/build/default/sample/Release/D3D12Sample_02_ComputeGrayscale.exe
...
out/build/default/sample/Release/D3D12Sample_18_ProcessingCustomFusedShader.exe
out/build/default/sample/Release/D3D12Sample_19_TypedCommandList.exe
```

---

## 実行例

```bat
out\build\default\sample\Release\D3D12Sample_01_HelloDevice.exe
out\build\default\sample\Release\D3D12Sample_07_ProcessingFusedConvertResize.exe
out\build\default\sample\Release\D3D12Sample_08_ProcessingP010Rgba16.exe
out\build\default\sample\Release\D3D12Sample_18_ProcessingCustomFusedShader.exe
out\build\default\sample\Release\D3D12Sample_19_TypedCommandList.exe
```

`RESULT: OK`を出すサンプルは、GPU実行・同期・readbackまたはcommand lifecycle確認まで完了しています。

環境依存機能を必要とするサンプルは、対応FormatやUAV/SRV capabilityがない場合に処理をskipして正常終了することがあります。

---

## 自分のプロジェクトへの組み込み

CMakeを使う場合:

```cmake
set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

Visual Studioへ直接追加する場合:

1. include pathに`include/`と`include/D3D12Helper/`を追加する。
2. `src/*.cpp`をcompile対象へ追加する。
3. Processingを使う場合は`shaders/D3D12Processing/`を配置する。
4. DXCを使う場合はruntime DLLをexe横またはPATH上へ配置する。
