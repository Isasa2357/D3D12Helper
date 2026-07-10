# Generic GPU Foundation Phase 2

この文書は、外部所有 `ID3D12Resource` を AddRef せず扱うための非所有 view と、明示 Resource State を用いる Processing 経路の設計境界を記録します。

## `D3D12ResourceView`

`D3D12ResourceView` は `ID3D12Resource*` だけを保持する非所有型です。

- AddRef / Release を行わない。
- Resource State を保持しない。
- pointer-sized。
- trivially copyable / trivially destructible。
- raw `ID3D12Resource*` または既存 `D3D12Resource` から明示的に生成する。
- 参照先 Resource は、view を利用する処理と投入済み GPU work が完了するまで呼び出し側が保持する。

この型には `SetState()` を追加しません。複数 view 間で独立した state cache が分岐することを防ぎます。

## Validation

既存 raw-pointer API を変更・overloadせず、別名で次を追加します。

- `ValidateTexture2DView()`
- `ValidateTexture2DViewOrThrow()`

検証内容は既存 `ValidateTexture2D()` と同じです。

## Processing descriptor view

Texture view 作成には `FromView` suffix の非所有経路を追加します。

- `CreateRgbaTextureViewSetFromView()`
- `CreateYuv420SrvViewSetFromView()`
- `CreateYuv420UavViewSetFromView()`
- `CreateYuv420SrvUavViewSetFromView()`

既存の `D3D12Resource` 版は削除・overloadしません。

## Processing shortcut API

現在、次の非所有経路を提供します。

- `D3D12FormatConverter::RecordConvertView()`
- `D3D12FusedProcessor::RecordConvertResizeView()`
- `D3D12Resizer::RecordResizeView()`
- `D3D12Remapper::RecordRemapView()`
- `D3D12Compositor::RecordCompositeView()`

既存メソッドと同名の overload は追加せず、`View` suffix の別名APIとします。これにより、v1.12.1 で可能だったメンバー関数ポインタ取得の一意性を維持します。

## 明示 State の必須化

すべての非所有 Processing API では `useExplicitStates == true` が必須です。

```cpp
D3D12ProcessingStateDesc state;
state.useExplicitStates = true;
state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
state.srcAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
state.dstBefore = D3D12_RESOURCE_STATE_COMMON;
state.dstAfter  = D3D12_RESOURCE_STATE_COMMON;
```

非所有経路では次を行いません。

- `D3D12Resource::GetState()` への依存
- `D3D12Resource::SetState()` による state cache 更新
- Resource lifetime の延長

2入力APIで同一Resourceを両入力へ渡す場合、両入力のbefore / after stateは一致していなければなりません。

## 内部 adapter

Resize / Remap / Composite の View API は、既存の owned-resource 実装を再利用するため、呼び出し中だけ有効な scoped adapter を使用します。

- adapter は AddRef / Release を行わない。
- adapter は例外送出時を含めて raw pointer を detach してから破棄される。
- adapter は呼び出し外へ保存されない。
- View API が明示stateを必須とするため、adapter内の独立state cacheは参照・更新されない。

## Barrier Layer

`D3D12BarrierBatch` は Layer 1 のため、Layer 2 の `D3D12ResourceView` へ依存させません。

```cpp
batch.Transition(view.Get(), before, after);
```

raw pointer を渡す既存APIで対応します。

## Lifetime

外部 owner は少なくとも次の期間、Resource を生存させる必要があります。

1. descriptor 作成中
2. command 記録中
3. command queue へ投入後、対象Fence完了まで

`D3D12ResourceView` を非同期ジョブへ保存する場合、上位層は別途Resource所有権を保持する必要があります。

## 互換性

- v1.12.1 の既存公開メソッドを削除・変更しない。
- 同名overloadを追加しない。
- owned `D3D12Resource` 経路の単一 state cache を維持する。
- `CompatibilityV1121` suite で既存 Processing メソッドの型を compile-time 検証する。

## 次段階

本変更の Debug / Release テスト後、構造が近い単一入力・単一出力processorへ展開します。

- `D3D12ColorAdjuster`
- `D3D12KernelFilter`
- `D3D12RegionEffectProcessor`

scratch resourceや3入力以上を扱うProcessorは、専用state descriptorとlifetime条件を確認してから段階的に対応します。
