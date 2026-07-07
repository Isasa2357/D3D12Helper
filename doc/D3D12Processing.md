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
states.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
states.srcAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
states.dstBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
states.dstAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;

processor.RecordSomething(ctx, src, dst, desc, states);
```

---

## 共通制約

| 項目 | 方針 |
| --- | --- |
| in-place | 基本的に非対応。`src` と `dst` は別 resource にする |
| output texture | UAV 書き込み対象なので `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` が必要 |
| shader directory | 実行時に `D3D12Helper/shaders/D3D12Processing/` を参照可能にする |
| descriptor heap | shader-visible CBV/SRV/UAV heap が必要 |
| format | 多くの pass は RGBA-like texture が対象 |
| state | default では `D3D12Resource` の簡易 state tracking を使う |
| rect | `{0,0,0,0}` は texture 全体として解決される |

---

## API 一覧

| API | 入力 | 出力 | 概要 |
| --- | --- | --- | --- |
| `D3D12FormatConverter` | RGBA-like / NV12 / P010 | RGBA-like / NV12 / P010 | format conversion |
| `D3D12Resizer` | RGBA-like | RGBA-like | point / linear resize |
| `D3D12Remapper` | RGBA-like + R32G32 map | RGBA-like | remap |
| `D3D12Compositor` | base + overlay | RGBA-like | copy / alpha blend / add |
| `D3D12FusedProcessor` | RGBA-like / NV12 / P010 | RGBA-like | convert + resize |
| `D3D12Blurrer` | RGBA-like | RGBA-like | Gaussian / Box blur |
| `D3D12RegionEffectProcessor` | RGBA-like | RGBA-like | region effect |
| `D3D12RegionBlur` | RGBA-like | RGBA-like | region masked blur |
| `D3D12ColorAdjuster` | RGBA-like | RGBA-like | brightness / contrast / gamma / saturation |
| `D3D12KernelFilter` | RGBA-like | RGBA-like | 3x3 kernel |
| `D3D12MaskProcessor` | RGBA-like masks | RGBA-like | apply / blend / combine / invert |
| `D3D12ThresholdProcessor` | RGBA-like | RGBA-like | threshold / heatmap / color map / overlay |
| `D3D12PyramidProcessor` | RGBA-like | RGBA-like | downsample2x / upsample2x |
| `D3D12PyramidBlur` | RGBA-like | RGBA-like | accelerated approximate blur |
| `D3D12PyramidRegionBlur` | RGBA-like | RGBA-like | accelerated region blur |

---

## D3D12FormatConverter

Format conversion 専用 pass です。リサイズは行いません。`srcRect` と `dstRect` のサイズが異なる場合は例外になります。

対応変換:

- RGBA-like → RGBA-like
- NV12 → RGBA-like
- P010 → RGBA-like
- RGBA-like → NV12
- RGBA-like → P010

```cpp
D3D12FormatConverter converter;
converter.Initialize(processing);

auto dst = converter.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

FormatConvertDesc desc;
desc.srcFormat = DXGI_FORMAT_NV12;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.color.srcMatrix = ProcessingColorMatrix::BT709;
desc.color.srcRange = ProcessingColorRange::Full;

converter.RecordConvert(ctx, nv12Texture, dst, desc);
```

---

## D3D12Resizer

RGBA-like texture の resize pass です。

対応 filter:

- `ProcessingFilter::Point`
- `ProcessingFilter::Linear`

```cpp
D3D12Resizer resizer;
resizer.Initialize(processing);

auto resized = resizer.CreateOutputTexture(*core, dstWidth, dstHeight, DXGI_FORMAT_R8G8B8A8_UNORM);

ResizeDesc desc;
desc.filter = ProcessingFilter::Linear;

resizer.RecordResize(ctx, srcRgba, resized, desc);
```

---

## D3D12Remapper

map texture による座標変換 pass です。

- 入力: RGBA-like
- map: `DXGI_FORMAT_R32G32_FLOAT`
- 出力: RGBA-like

```cpp
D3D12Remapper remapper;
remapper.Initialize(processing);

RemapDesc desc;
desc.mapFormat = DXGI_FORMAT_R32G32_FLOAT;
desc.filter = ProcessingFilter::Linear;
desc.coordinateMode = RemapCoordinateMode::AbsolutePixels;
desc.borderMode = RemapBorderMode::Clamp;

remapper.RecordRemap(ctx, src, mapTexture, dst, desc);
```

---

## D3D12Compositor

2 枚の RGBA-like texture を合成します。

対応 blend mode:

- `CompositeBlendMode::Copy`
- `CompositeBlendMode::AlphaBlend`
- `CompositeBlendMode::PremultipliedAlpha`
- `CompositeBlendMode::Add`

```cpp
D3D12Compositor compositor;
compositor.Initialize(processing);

CompositeDesc desc;
desc.blendMode = CompositeBlendMode::AlphaBlend;
desc.opacity = 0.75f;

compositor.RecordComposite(ctx, base, overlay, dst, desc);
```

---

## D3D12FusedProcessor

format conversion と resize を 1 dispatch で実行します。

対応:

- RGBA-like → RGBA-like
- NV12 → RGBA-like
- P010 → RGBA-like

```cpp
D3D12FusedProcessor fused;
fused.Initialize(processing);

FusedConvertResizeDesc desc;
desc.srcFormat = DXGI_FORMAT_NV12;
desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.filter = ProcessingFilter::Linear;

fused.RecordConvertResize(ctx, srcNv12, dstRgba, desc);
```

---

## D3D12Blurrer

RGBA-like texture に Gaussian / Box blur をかけます。separable 2-pass のため `scratch` が必要です。

```cpp
D3D12Blurrer blurrer;
blurrer.Initialize(processing);

auto scratch = blurrer.CreateScratchTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
auto dst = blurrer.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

BlurDesc desc;
desc.mode = BlurMode::Gaussian;
desc.radius = 8;
desc.sigma = 3.0f;

blurrer.RecordBlur(ctx, src, scratch, dst, desc);
```

---

## D3D12RegionEffectProcessor

円形または矩形領域に effect を適用します。

対応 shape:

- `RegionShape::Circle`
- `RegionShape::Rect`

対応 selection:

- `RegionSelection::Inside`
- `RegionSelection::Outside`

対応 effect:

- `RegionEffectMode::Darken`
- `RegionEffectMode::Tint`
- `RegionEffectMode::Grayscale`
- `RegionEffectMode::Highlight`
- `RegionEffectMode::AlphaFade`
- `RegionEffectMode::Vignette`

```cpp
D3D12RegionEffectProcessor effect;
effect.Initialize(processing);

RegionEffectDesc desc;
desc.shape = RegionShape::Circle;
desc.selection = RegionSelection::Outside;
desc.effect = RegionEffectMode::Darken;
desc.centerX = width * 0.5f;
desc.centerY = height * 0.5f;
desc.radius = 160.0f;
desc.edgeSoftness = 24.0f;
desc.strength = 0.5f;

effect.RecordRegionEffect(ctx, src, dst, desc);
```

---

## D3D12RegionBlur

元画像を blur した中間 texture と、元画像を region mask で合成します。通常 blur よりも直感的に「円内だけ鮮明」「円外だけ blur」などを実装できます。

```cpp
D3D12RegionBlur regionBlur;
regionBlur.Initialize(processing);

auto scratch = regionBlur.CreateScratchTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
auto blurred = regionBlur.CreateBlurredTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
auto dst = regionBlur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

RegionBlurDesc desc;
desc.shape = RegionShape::Circle;
desc.selection = RegionSelection::Outside;
desc.centerX = width * 0.5f;
desc.centerY = height * 0.5f;
desc.radius = 160.0f;
desc.edgeSoftness = 24.0f;
desc.blurStrength = 1.0f;
desc.blurMode = BlurMode::Gaussian;
desc.blurRadius = 8;
desc.blurSigma = 3.0f;

regionBlur.RecordRegionBlur(ctx, src, scratch, blurred, dst, desc);
```

---

## D3D12ColorAdjuster

brightness / contrast / gamma / saturation を 1 pass で調整します。

```cpp
D3D12ColorAdjuster adjuster;
adjuster.Initialize(processing);

ColorAdjustDesc desc;
desc.brightness = 0.05f;
desc.contrast = 1.2f;
desc.gamma = 1.0f;
desc.saturation = 0.8f;
desc.preserveAlpha = true;

adjuster.RecordColorAdjust(ctx, src, dst, desc);
```

---

## D3D12KernelFilter

3x3 kernel filter です。

対応 mode:

- `KernelFilterMode::Custom3x3`
- `KernelFilterMode::Sharpen`
- `KernelFilterMode::EdgeDetect`

```cpp
D3D12KernelFilter filter;
filter.Initialize(processing);

KernelFilterDesc desc;
desc.mode = KernelFilterMode::Sharpen;
desc.edgeMode = KernelEdgeMode::Clamp;
desc.preserveAlpha = true;

filter.RecordKernelFilter(ctx, src, dst, desc);
```

---

## D3D12MaskProcessor

mask texture を使う処理をまとめた processor です。

対応:

- `RecordApplyMask`
- `RecordBlendByMask`
- `RecordCombineMasks`
- `RecordInvertMask`

mask channel:

- `MaskChannel::Red`
- `MaskChannel::Green`
- `MaskChannel::Blue`
- `MaskChannel::Alpha`
- `MaskChannel::Luma`

```cpp
D3D12MaskProcessor masks;
masks.Initialize(processing);

MaskApplyDesc desc;
desc.mode = MaskApplyMode::MultiplyRgb;
desc.channel = MaskChannel::Alpha;
desc.strength = 1.0f;

masks.RecordApplyMask(ctx, src, mask, dst, desc);
```

Blend by mask:

```cpp
MaskBlendDesc desc;
desc.channel = MaskChannel::Alpha;
desc.opacity = 1.0f;

masks.RecordBlendByMask(ctx, base, overlay, mask, dst, desc);
```

Combine masks:

```cpp
MaskCombineDesc desc;
desc.mode = MaskCombineMode::Max;
desc.channelA = MaskChannel::Alpha;
desc.channelB = MaskChannel::Alpha;

masks.RecordCombineMasks(ctx, maskA, maskB, dst, desc);
```

---

## D3D12ThresholdProcessor

二値化、範囲二値化、confidence heatmap、class color map、mask overlay を提供します。

```cpp
D3D12ThresholdProcessor threshold;
threshold.Initialize(processing);

ThresholdDesc desc;
desc.channel = MaskChannel::Luma;
desc.threshold = 0.5f;
desc.foregroundColor[0] = 1.0f;
desc.foregroundColor[1] = 1.0f;
desc.foregroundColor[2] = 1.0f;
desc.foregroundColor[3] = 1.0f;

threshold.RecordThreshold(ctx, src, dst, desc);
```

Heatmap:

```cpp
ConfidenceHeatmapDesc desc;
desc.channel = MaskChannel::Red;
desc.mode = HeatmapMode::TurboApprox;
desc.minValue = 0.0f;
desc.maxValue = 1.0f;
desc.opacity = 1.0f;

threshold.RecordConfidenceHeatmap(ctx, confidence, heatmap, desc);
```

Class color map:

```cpp
ClassColorMapDesc desc;
desc.channel = MaskChannel::Red;
desc.classScale = 255.0f;
desc.classCount = 16;
desc.opacity = 1.0f;

threshold.RecordClassColorMap(ctx, classIdTexture, colored, desc);
```

Mask overlay:

```cpp
MaskOverlayDesc desc;
desc.channel = MaskChannel::Alpha;
desc.overlayColor[0] = 1.0f;
desc.overlayColor[1] = 0.0f;
desc.overlayColor[2] = 0.0f;
desc.overlayColor[3] = 0.5f;

threshold.RecordMaskOverlay(ctx, mask, overlay, desc);
```

---

## D3D12PyramidProcessor

2x downsample / 2x upsample の primitive です。`D3D12PyramidBlur` の内部でも使われます。

```cpp
D3D12PyramidProcessor pyramid;
pyramid.Initialize(processing);

auto half = pyramid.CreateDownsampledTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
auto restored = pyramid.CreateUpsampledTexture(*core, halfWidth, halfHeight, DXGI_FORMAT_R8G8B8A8_UNORM);

PyramidDownsampleDesc down;
pyramid.RecordDownsample2x(ctx, src, half, down);

PyramidUpsampleDesc up;
up.filter = ProcessingFilter::Linear;
pyramid.RecordUpsample2x(ctx, half, restored, up);
```

---

## D3D12PyramidBlur

大半径 blur を高速化する approximate blur です。`downsample -> low-res blur -> upsample` を行います。

```cpp
D3D12PyramidBlur blur;
blur.Initialize(processing);

auto workspace = blur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
auto dst = blur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

PyramidBlurDesc desc;
desc.levels = 2;
desc.blurMode = BlurMode::Gaussian;
desc.blurRadius = 8;
desc.blurSigma = 3.0f;
desc.upsampleFilter = ProcessingFilter::Linear;

blur.RecordPyramidBlur(ctx, src, workspace, dst, desc);
```

注意:

- `levels` は `D3D12PyramidBlur::MaxLevels` 以下にする。
- 処理 rect は `2^levels` で割り切れるサイズにする。
- workspace は `levels`、`width`、`height`、`format` に対応するものを使う。

---

## D3D12PyramidRegionBlur

`D3D12PyramidBlur` によって低コストで作った blurred texture と元画像を region mask で合成します。

```cpp
D3D12PyramidRegionBlur blur;
blur.Initialize(processing);

auto workspace = blur.CreateWorkspace(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
auto dst = blur.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

PyramidRegionBlurDesc desc;
desc.levels = 2;
desc.shape = RegionShape::Circle;
desc.selection = RegionSelection::Outside;
desc.centerX = width * 0.5f;
desc.centerY = height * 0.5f;
desc.radius = 160.0f;
desc.edgeSoftness = 24.0f;
desc.blurStrength = 1.0f;
desc.blurMode = BlurMode::Gaussian;
desc.blurRadius = 8;
desc.blurSigma = 3.0f;
desc.upsampleFilter = ProcessingFilter::Linear;

blur.RecordPyramidRegionBlur(ctx, src, workspace, dst, desc);
```

---

## D3D11Helper との対応

| D3D12Helper | D3D11Helper | 備考 |
| --- | --- | --- |
| `D3D12FormatConverter` | `D3D11FormatConverter` | `RecordConvert` vs `DispatchConvert` |
| `D3D12Resizer` | `D3D11Resizer` | `RecordResize` vs `DispatchResize` |
| `D3D12Remapper` | `D3D11Remapper` | `RecordRemap` vs `DispatchRemap` |
| `D3D12Compositor` | `D3D11Compositor` | `RecordComposite` vs `DispatchComposite` |
| `D3D12FusedProcessor` | `D3D11FusedProcessor` | fused convert + resize |
| `D3D12Blurrer` | `D3D11Blurrer` | separable blur |
| `D3D12RegionEffectProcessor` | `D3D11RegionEffectProcessor` | region effect |
| `D3D12RegionBlur` | `D3D11RegionBlur` | region blur |
| `D3D12ColorAdjuster` | `D3D11ColorAdjuster` | color adjustment |
| `D3D12KernelFilter` | `D3D11KernelFilter` | 3x3 kernel |
| `D3D12MaskProcessor` | `D3D11MaskProcessor` | mask processing |
| `D3D12ThresholdProcessor` | `D3D11ThresholdProcessor` | threshold / visualization |
| `D3D12PyramidProcessor` | `D3D11PyramidProcessor` | downsample / upsample |
| `D3D12PyramidBlur` | `D3D11PyramidBlur` | accelerated blur |
| `D3D12PyramidRegionBlur` | `D3D11PyramidRegionBlur` | accelerated region blur |

---

## HLSL library 方針

Processing Layer の HLSL は repository 内では `shaders/D3D12Processing/` に配置します。実行時 asset としては `D3D12Helper/shaders/D3D12Processing/` のように helper-specific root の下へ配置してください。単体 processor はそのまま使える `.hlsl` を提供し、共通処理は `.hlsli` として分離します。

連続処理を 1 dispatch にまとめたい場合は、必要な `.hlsli` や関数を利用して、アプリケーション側で fused shader を作る方針です。ライブラリ側はまず安全で検証しやすい primitive pass を提供します。

---

## 今後の拡張

将来候補は [`D3D12ProcessingFutureWork.md`](D3D12ProcessingFutureWork.md) を参照してください。
