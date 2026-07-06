# D3D12Processing Future Work

このファイルは、v0.2.0 時点では未実装、または安定 API として切らない将来候補をまとめるためのメモです。

D3D12Helper v0.2.0 では、D3D11Helper 側で実装済みだった主要な Processing API を D3D12 側へ一通り移植しました。以下は、その次の段階で検討する候補です。

---

## 1. MorphologyProcessor

### 候補 API

- `D3D12MorphologyProcessor`
- `MorphologyDesc`

### 候補機能

- Erode
- Dilate
- Open
- Close
- MinFilter
- MaxFilter

### 目的

segmentation mask、binary mask、confidence map の後処理を GPU 上で完結させるための機能です。`D3D12MaskProcessor`、`D3D12ThresholdProcessor` の後段として使います。

### 注意点

- kernel size を任意にする場合は separable でないため負荷が高い。
- まずは 3x3 / 5x5 の固定 kernel から始めるのが安全。
- R8 / R16 系 format を直接扱うか、RGBA-like の channel として扱うかを設計する必要がある。

---

## 2. LUT / ToneMapping

### 候補 API

- `D3D12LutProcessor`
- `D3D12ToneMapper`

### 候補機能

- 1D LUT
- 3D LUT
- Linear → sRGB
- sRGB → Linear
- HDR → SDR tone mapping
- False color

### 目的

RGBA16F や HDR 寄りの処理、カメラ画像の color grading、confidence map 可視化の品質向上に使います。

### 注意点

- 3D LUT は texture3D / sampler の扱いが必要。
- 現在の compute pipeline template は SRV / UAV / root constants 中心なので、sampler table の設計を整理する必要がある。

---

## 3. YUV direct processing extensions

### 候補機能

- NV12 / P010 に直接 darken
- NV12 / P010 に直接 resize
- Y plane only blur
- Y plane only sharpen
- YUV → process → YUV の fused path

### 目的

動画処理性能を詰める場合、NV12/P010 を一度 RGBA に変換せず、YUV のまま処理したい場面があります。

### 注意点

- chroma plane の解像度が異なる。
- 色空間と range の扱いを間違えると画質劣化する。
- UAV plane view のサポート可否が GPU / driver に依存する。
- D3D11Helper / D3D12Helper の API 対応を崩さない設計が必要。

---

## 4. ProcessingGraph / FusedEffectProcessor

### 候補 API

- `D3D12ProcessingGraph`
- `D3D12FusedEffectProcessor`
- `D3D12ProcessingPass`
- `D3D12ProcessingGraphCompiler`

### 目的

複数の Processing pass を連続実行する場合の中間 texture と dispatch 数を削減します。

例:

```text
NV12 -> RGBA -> remap -> resize -> region effect -> output
```

これを単純に実行すると複数 dispatch と複数中間 texture が必要です。処理内容によっては 1 shader に融合できます。

### 方針

v0.2.0 では processor を個別 primitive として揃えることを優先しました。graph / fused 化は、primitive API が安定してから行うのがよいです。

### 注意点

- shader 生成をするか、関数 library を提供してユーザーが fused shader を書くか。
- resource lifetime / temporary texture allocator が必要。
- barrier の自動挿入規則が必要。
- デバッグのしやすさが落ちる可能性がある。

---

## 5. ProcessingBenchmark

### 候補内容

- GPU timestamp query
- 1920x1080 / 3840x2160
- FormatConvert / Resize / Blur / RegionBlur / PyramidBlur
- average / min / max
- dispatch count
- temporary texture count

### 目的

処理追加後の性能評価と regression 確認を行うための benchmark です。

### 優先度

高いです。Processing API が増えたため、v0.2.x の早い段階で benchmark sample を追加すると、設計判断がしやすくなります。

---

## 6. Documentation / sample improvements

### 候補

- 各 processor の図解
- resource state 例
- explicit state を使うケースの例
- Varjo / camera / encoder pipeline との接続例
- D3D11Helper との migration guide

---

## 7. Release candidates after v0.2.0

### v0.2.1 候補

- doc typo 修正
- sample の追加
- benchmark sample
- small validation fix

### v0.3.0 候補

- MorphologyProcessor
- LUT / ToneMapping
- ProcessingBenchmark
- YUV direct processing の一部

### v0.4.0 候補

- ProcessingGraph
- FusedEffectProcessor
- temporary texture allocator
