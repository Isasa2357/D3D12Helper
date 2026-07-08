# D3D12Processing Workflow

`D3D12Processing` は、resize / remap / blur などの単体機能だけを提供する薄い wrapper 集ではありません。

Processing Layer の本体は、次の要素をまとめた **HLSL library based processing layer** です。

- `shaders/D3D12Processing/*.hlsli` にある HLSL 関数ライブラリ
- format / color space / plane view の共通定義
- SRV / UAV descriptor table 作成
- root constants / root signature / PSO の実行基盤
- resource state transition / UAV barrier の補助
- よく使う単体 pass を呼ぶための C++ shortcut processor

## 単体 Processor はショートカット

`D3D12FormatConverter`、`D3D12Resizer`、`D3D12Remapper`、`D3D12Blurrer` などは、Processing Layer の全体像そのものではありません。これらは、よく使う primitive pass をすぐ実行するための **ショートカット API** です。

単体 pass の確認、試作、または本当に 1 つの処理だけで足りる場合は、これらの C++ processor を使います。

```cpp
D3D12Resizer resizer;
resizer.Initialize(processing);
resizer.RecordResize(ctx, src, dst, desc);
```

## 実務的な連続処理は fused HLSL にまとめる

実アプリでは、次のような連続処理がよく発生します。

```text
NV12 -> RGB -> resize -> mask/effect
RGBA -> remap -> color adjust -> overlay
camera texture -> undistort -> crop -> highlight
```

このような処理を C++ processor で 1 pass ずつ呼ぶと、中間 texture、複数 dispatch、複数 barrier が増えます。性能や遅延が重要な場合は、アプリ側で HLSL を書き、Processing の `.hlsli` を include して 1 dispatch に融合する設計を推奨します。

```hlsl
#include "ProcessingCommon.hlsli"
#include "ColorSpace.hlsli"

// app-owned fused shader
// NV12 -> RGB -> resize -> custom effect
```

## 推奨パターン

| 状況 | 推奨 |
| --- | --- |
| format conversion だけ | `D3D12FormatConverter` |
| resize だけ | `D3D12Resizer` |
| convert + resize だけ | `D3D12FusedProcessor` |
| convert + resize + 独自 effect | アプリ側 custom fused HLSL |
| remap + color adjust + overlay | アプリ側 custom fused HLSL |
| 中間 texture / barrier / dispatch を減らしたい | アプリ側 custom fused HLSL |

## custom fused HLSL の基本手順

1. `D3D12ProcessingContext` を初期化する。
2. `ProcessingCommon.hlsli` や `ColorSpace.hlsli` を include した独自 `.hlsl` を用意する。
3. `CompileShaderFromFile` で compile する。
4. `D3D12ComputePipeline::InitializeWithTemplate` で、必要な SRV / UAV 数と root constants 数を指定する。
5. `CreateYuv420SrvViewSet`、`CreateRgbaTextureViewSet` などで descriptor を作る。
6. `NON_PIXEL_SHADER_RESOURCE` / `UNORDERED_ACCESS` へ transition する。
7. root descriptor table と root constants を設定し、dispatch する。
8. UAV barrier または後続用途への transition を入れる。

## サンプル

`sample/18_ProcessingCustomFusedShader` は、D3D12Processing の HLSL library を include したアプリ側 shader で、次の処理を 1 dispatch にまとめます。

```text
NV12 -> RGB -> resize -> circular outside-region darken
```

このサンプルは意図的に `D3D12FusedProcessor` を使わず、`D3D12ComputePipeline` と Processing の view / descriptor helpers を直接使います。これは、単体 C++ processor がショートカットであり、実務的な連続処理では HLSL library を使ってアプリ側で fused pass を作る、という設計意図を示すためです。
