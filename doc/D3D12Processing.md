# D3D12Processing - Layer 3 Processing API

`D3D12Processing` は、D3D12Helper の Layer 3 として実装された GPU 画像処理レイヤーです。Layer 1 / 2 の `D3D12Core`、`D3D12CommandContext`、`D3D12Resource`、`D3D12DescriptorAllocator`、`D3D12ComputePipeline`、`D3D12ShaderCompiler` を土台にして、フォーマット変換、リサイズ、リマップ、合成、blur、mask、threshold、pyramid blur などを提供します。

この document は v0.2.0 時点の Processing API を対象にしています。

---

## 位置づけ

```text
Application / Camera / Renderer / Encoder / ML Pipeline
    |
    v
Layer 3 : D3D12Processing
    FormatConvert / Resize / Remap / Composite / Fused
    Blur / RegionEffect / RegionBlur
    ColorAdjust / KernelFilter
    Mask / Threshold
    Pyramid / PyramidBlur / PyramidRegionBlur
    HLSL shader library
    |
    v
Layer 2 : D3D12Framework
    Resource / Descriptor / Upload / Readback / Pipeline
    |
    v
Layer 1 : D3D12Core
    Device / Queue / Fence / CommandContext / Barrier
```

---

## インクルード

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
```

短い include path を使う場合：

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
#include "D3D12Processing/D3D12Processing.hpp"
```

---

## 初期化

Processing Layer は、既存の `D3D12Core` と descriptor allocator を受け取ります。

```cpp
using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

D3D12CoreConfig cfg;
cfg.allowWarpAdapter = true;
auto core = D3D12Core::CreateShared(cfg);

D3D12DescriptorAllocator cbvSrvUav;
cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048, true);

D3D12DescriptorAllocator sampler;
sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);

D3D12ProcessingContext processing;
processing.Initialize(*core, &cbvSrvUav, &sampler, "D3D12Helper/shaders/D3D12Processing");
```

`shaderDirectory` を省略した場合、`D3D12ProcessingContext` はまず `D3D12Helper/shaders/D3D12Processing` を探します。見つからない場合は、既存アプリとの互換性のために従来の `shaders/D3D12Processing` を fallback として参照します。

`CBV/SRV/UAV` 用 descriptor heap は shader-visible である必要があります。Processing pass は内部で SRV/UAV descriptor table を作成し、compute shader から参照します。

---

## 対応 format

### RGBA-like format

- `DXGI_FORMAT_R8G8B8A8_UNORM`
- `DXGI_FORMAT_B8G8R8A8_UNORM`
- `DXGI_FORMAT_R16G16B16A16_FLOAT`

### YUV 4:2:0 format

- `DXGI_FORMAT_NV12`
- `DXGI_FORMAT_P010`

Plane view は Processing Layer 側で作成します。

| format | Y plane view | UV plane view |
| --- | --- | --- |
| `NV12` | `R8_UNORM` | `R8G8_UNORM` |
| `P010` | `R16_UNORM` | `R16G16_UNORM` |

### map texture

- `DXGI_FORMAT_R32G32_FLOAT`

Remap の map texture は、destination pixel ごとの source coordinate を `float2` で持ちます。

---

## 共通 descriptor / state 方針

Processing pass は `Record*` 関数で command list に処理を記録します。内部では SRV/UAV view を確保し、必要な resource barrier を積みます。

通常は `D3D12Resource` が持つ単一 state tracking を使います。

```cpp
converter.RecordConvert(ctx, src, dst, desc);
```

外部 API 共有、複数 queue 連携、既存 command list との混在などで状態を明示したい場合は `D3D12ProcessingStateDesc`、`D3D12ProcessingTwoInputStateDesc`、`D3D12ProcessingThreeInputStateDesc`、`D3D12ProcessingBlurStateDesc` などを渡します。

```cpp
D3D12ProcessingStateDesc states;
states.useExplicitStates = true;
```
