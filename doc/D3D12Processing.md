# D3D12Processing - Layer 3 Processing API

`D3D12Processing` は、D3D12Helper の Layer 3 として実装された GPU 画像処理レイヤーです。Layer 1 / 2 の `D3D12Core`、`D3D12CommandContext`、`D3D12Resource`、`D3D12DescriptorAllocator`、`D3D12ComputePipeline`、`D3D12ShaderCompiler` を土台にして、フォーマット変換、リサイズ、リマップ、合成、fused pass を提供します。

Layer 1 / 2 は汎用 D3D12 操作を担当し、Processing 固有の format / plane view / HLSL / validation はこの Layer 3 が担当します。

---

## 位置づけ

```text
+--------------------------------------------------------------+
|  Application / Camera / Renderer / Encoder / ML Pipeline      |
+--------------------------------------------------------------+
                     |
                     v
+--------------------------------------------------------------+
|  Layer 3 : D3D12Processing                                    |
|    D3D12ProcessingContext                                     |
|    D3D12TextureViews                                          |
|    D3D12FormatConverter                                       |
|    D3D12Resizer                                               |
|    D3D12Remapper                                              |
|    D3D12Compositor                                            |
|    D3D12FusedProcessor                                        |
|    shaders/D3D12Processing/*.hlsl / *.hlsli                  |
+--------------------------------------------------------------+
                     |
                     v
+--------------------------------------------------------------+
|  Layer 2 : D3D12Framework                                     |
|    Resource / Descriptor / Upload / Readback / Pipeline       |
+--------------------------------------------------------------+
                     |
                     v
+--------------------------------------------------------------+
|  Layer 1 : D3D12Core                                          |
|    Device / Queue / Fence / CommandContext / Barrier          |
+--------------------------------------------------------------+
```

---

## インクルード

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
```

既存サンプルやテストと同じ短い include path を使う場合は次の形式でもよいです。

```cpp
#include "D3D12Core/D3D12Core.hpp"
#include "D3D12Framework/D3D12Framework.hpp"
#include "D3D12Processing/D3D12Processing.hpp"
```

---

## 初期化

Processing Layer は、D3D12 デバイスそのものではなく、既存の `D3D12Core` と descriptor allocator を受け取ります。

```cpp
using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

D3D12CoreConfig cfg;
cfg.allowWarpAdapter = true;
auto core = D3D12Core::CreateShared(cfg);

D3D12DescriptorAllocator cbvSrvUav;
cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true);

D3D12DescriptorAllocator sampler;
sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);

D3D12ProcessingContext processing;
processing.Initialize(*core, &cbvSrvUav, &sampler, "shaders/D3D12Processing");
```

`CBV/SRV/UAV` 用 descriptor heap は shader-visible である必要があります。Processing pass は内部で SRV/UAV descriptor table を作成し、compute shader から参照します。

---

## 対応 format

### RGBA-like format

- `DXGI_FORMAT_R8G8B8A8_UNORM`
- `DXGI_FORMAT_B8G8R8A8_UNORM`
- `DXGI_FORMAT_R16G16B16A16_FLOAT`

`B8G8R8A8_UNORM` は typed SRV/UAV の format conversion に任せます。HLSL 側で手動 swizzle しません。

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

planar texture、外部 API 共有、複数 queue 連携などで状態を明示したい場合は `D3D12ProcessingStateDesc` または `D3D12ProcessingTwoInputStateDesc` を渡します。

```cpp
D3D12ProcessingStateDesc states;
states.useExplicitStates = true;
states.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
states.srcAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
states.dstBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
states.dstAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;

converter.RecordConvert(ctx, src, dst, desc, states);
```

---

## D3D12FormatConverter

`D3D12FormatConverter` は format conversion 専用 pass です。リサイズは行いません。`srcRect` と `dstRect` のサイズが異なる場合は例外になります。

対応変換:

- RGBA-like → RGBA-like
- NV12 → RGBA-like
- P010 → RGBA-like
- RGBA-like → NV12
- RGBA-like → P010

```cpp
D3D12FormatConverter converter;
converter.Initialize(processing);

auto dst = converter.CreateOutputTexture(
    *core,
    width,
    height,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_COMMON);

FormatConvertDesc desc;
desc.srcFormat = DXGI_FORMAT_NV12;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.color.srcMatrix = ProcessingColorMatrix::BT709;
desc.color.srcRange = ProcessingColorRange::Full;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
converter.RecordConvert(ctx, nv12Texture, dst, desc);
ctx.Close();
```

色空間は `ProcessingColorDesc` で指定します。

```cpp
ProcessingColorDesc color;
color.srcMatrix = ProcessingColorMatrix::BT709;
color.srcRange  = ProcessingColorRange::Limited;
color.dstMatrix = ProcessingColorMatrix::BT709;
color.dstRange  = ProcessingColorRange::Full;
```

---

## D3D12Resizer

`D3D12Resizer` は RGBA-like texture の resize pass です。

対応 filter:

- `ProcessingFilter::Point`
- `ProcessingFilter::Linear`

```cpp
D3D12Resizer resizer;
resizer.Initialize(processing);

auto resized = resizer.CreateOutputTexture(
    *core,
    dstWidth,
    dstHeight,
    DXGI_FORMAT_R8G8B8A8_UNORM);

ResizeDesc desc;
desc.filter = ProcessingFilter::Linear;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
resizer.RecordResize(ctx, srcRgba, resized, desc);
ctx.Close();
```

YUV のまま resize する専用 pass ではありません。YUV → RGBA resize を 1 dispatch で行いたい場合は `D3D12FusedProcessor` を使います。

---

## D3D12Remapper

`D3D12Remapper` は map texture による座標変換 pass です。

対応 input / output:

- RGBA-like → RGBA-like

対応 map:

- `DXGI_FORMAT_R32G32_FLOAT`

coordinate mode:

- `RemapCoordinateMode::AbsolutePixels`
- `RemapCoordinateMode::NormalizedZeroToOne`

border mode:

- `RemapBorderMode::Clamp`
- `RemapBorderMode::Constant`

```cpp
D3D12Remapper remapper;
remapper.Initialize(processing);

auto dst = remapper.CreateOutputTexture(
    *core,
    width,
    height,
    DXGI_FORMAT_R8G8B8A8_UNORM);

RemapDesc desc;
desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.mapFormat = DXGI_FORMAT_R32G32_FLOAT;
desc.filter = ProcessingFilter::Linear;
desc.coordinateMode = RemapCoordinateMode::AbsolutePixels;
desc.borderMode = RemapBorderMode::Clamp;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
remapper.RecordRemap(ctx, src, mapTexture, dst, desc);
ctx.Close();
```

OpenCV の remap map を使う場合は、GPU texture に `float2(srcX, srcY)` をアップロードして `R32G32_FLOAT` として渡します。

---

## D3D12Compositor

`D3D12Compositor` は base texture と overlay texture を合成します。

対応 format:

- RGBA-like format

blend mode:

- `CompositeBlendMode::Copy`
- `CompositeBlendMode::AlphaBlend`
- `CompositeBlendMode::PremultipliedAlpha`
- `CompositeBlendMode::Add`

```cpp
D3D12Compositor compositor;
compositor.Initialize(processing);

auto dst = compositor.CreateOutputTexture(
    *core,
    width,
    height,
    DXGI_FORMAT_R8G8B8A8_UNORM);

CompositeDesc desc;
desc.blendMode = CompositeBlendMode::AlphaBlend;
desc.opacity = 1.0f;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
compositor.RecordComposite(ctx, base, overlay, dst, desc);
ctx.Close();
```

`opacity` は `[0, 1]` の範囲です。範囲外は validation error になります。

---

## D3D12FusedProcessor

`D3D12FusedProcessor` は、複数処理を 1 dispatch にまとめるための fused pass です。現在は convert + resize を対象にしています。

対応 fused pass:

- RGBA-like → RGBA-like resize
- NV12 → RGBA-like resize
- P010 → RGBA-like resize

```cpp
D3D12FusedProcessor fused;
fused.Initialize(processing);

auto dst = fused.CreateOutputTexture(
    *core,
    dstWidth,
    dstHeight,
    DXGI_FORMAT_R8G8B8A8_UNORM);

FusedConvertResizeDesc desc;
desc.srcFormat = DXGI_FORMAT_NV12;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.filter = ProcessingFilter::Linear;
desc.color.srcMatrix = ProcessingColorMatrix::BT709;
desc.color.srcRange = ProcessingColorRange::Full;

D3D12CommandContext ctx = core->CreateDirectContext();
ctx.Reset();
fused.RecordConvertResize(ctx, nv12Texture, dst, desc);
ctx.Close();
```

`NV12 -> RGBA -> resize` を 2 pass に分ける場合と比べて、中間 RGBA texture と追加 dispatch を避けられます。

---

## P010 と 16bit / float 出力

P010 は 10bit 有効値を 16bit lane に格納する 4:2:0 format です。Processing Layer では `R16_UNORM` / `R16G16_UNORM` plane view を作成し、HLSL 内で正規化値として扱います。

`R16G16B16A16_FLOAT` は RGBA-like format として扱えます。HDR や ML 前処理など、8bit UNORM より広い範囲を保持したい用途で使えます。

---

## HLSL 構成

Processing shader は `shaders/D3D12Processing/` に配置されます。

```text
shaders/D3D12Processing/
├─ ProcessingCommon.hlsli
├─ ProcessingExtendedCommon.hlsli
├─ ColorSpace.hlsli
├─ ConvertRgbToRgb.hlsl
├─ ConvertNv12ToRgb.hlsl
├─ ConvertRgbToNv12.hlsl
├─ ResizeRgba.hlsl
├─ RemapRgba.hlsl
├─ CompositeRgba.hlsl
├─ FusedRgbToRgbResize.hlsl
└─ FusedYuv420ToRgbResize.hlsl
```

CMake ビルドでは、テスト・サンプル実行時に shader directory が exe 横へコピーされます。直接プロジェクトへ組み込む場合は、実行時に `D3D12ProcessingContext::Initialize()` へ shader directory を渡してください。

---

## サンプル

Processing 関連 sample:

| sample | 内容 |
| --- | --- |
| `07_ProcessingFusedConvertResize` | NV12 → RGBA resize を fused pass で実行し、CPU readback で検証 |
| `08_ProcessingP010Rgba16` | P010 / RGBA16F 関連の Processing API を使う最小例 |

すべてコンソールサンプルです。ウィンドウは不要です。

---

## テスト

Processing 関連テストは `Processing` suite に入ります。

検証範囲:

- 型・rect validation
- capability query
- shader compile
- RGBA view / NV12 / P010 plane view validation
- RGBA copy / BGRA typed store の readback 検証
- NV12 → RGBA の readback 検証
- RGBA → NV12 の Y/UV plane readback 検証
- P010 → RGBA の readback 検証
- resize point の readback 検証
- remap / composite の readback 検証
- fused convert + resize の readback 検証

```bat
ctest --test-dir out/build/default -C Debug -R Processing --output-on-failure
```

---

## 制約と注意

- Processing pass は compute shader ベースです。graphics pipeline の pixel shader pass ではありません。
- In-place 処理はサポートしません。入力と出力は別 resource にしてください。
- 既定では `D3D12Resource` の単一 state tracking を使います。planar texture や外部共有 resource で状態が複雑な場合は explicit state desc を使ってください。
- 未対応 format / 未対応機能は fallback せず例外化します。
- `D3D12ProcessingContext` に渡す descriptor allocator は、Processing の lifetime 中に有効である必要があります。
- shader file は実行時に読まれます。配布時は `shaders/D3D12Processing` を exe から参照可能な場所へ配置してください。
