# Generic GPU Foundation Phase 2: Processing Expansion

この文書は、`D3D12ResourceView` を既存 Processing shortcut processor へ段階的に展開する際の設計境界を記録します。

## 今回追加する API

- `D3D12Resizer::RecordResizeView()`
- `D3D12Remapper::RecordRemapView()`
- `D3D12Compositor::RecordCompositeView()`

既存の `RecordResize()`、`RecordRemap()`、`RecordComposite()` には同名 overload を追加しません。v1.12.1 で可能だったメンバー関数ポインタ取得を一意なまま維持するため、非所有経路には `View` suffix を使用します。

## State の扱い

すべての View API で `useExplicitStates == true` が必須です。

- View 自体は Resource State を保持しない。
- View API は `D3D12Resource` の単一 state cache を参照・更新しない。
- before / after state は呼び出し側が指定する。
- 同じ Resource を2つの入力として渡す場合、両入力の before / after state は一致していなければならない。

## Lifetime の扱い

`D3D12ResourceView` は AddRef / Release を行いません。

呼び出し側は、次の期間に Resource を生存させる必要があります。

1. descriptor 作成中
2. command 記録中
3. command queue への投入後、GPU処理が完了するまで

View API の内部では、既存 owned-resource 実装を再利用するための例外安全な scoped adapter を使用します。adapter は AddRef せず、呼び出し終了または例外送出時に raw pointer を detach してから破棄されます。adapter 自体が外部へ保存されることはありません。

## 今回の対象

- RGBA-like resize
- RGBA-like + R32G32_FLOAT map による remap
- RGBA-like 2入力 composite

Format Convert と Fused Convert/Resize は先行して対応済みです。

## 互換性

- 既存公開メソッドの型・引数・戻り値を変更しない。
- 既存 owned-resource 経路の自動 state cache 更新を維持する。
- 新しい View API では自動 state tracking を行わない。
- `CompatibilityV1121` suite で既存3メソッドのシグネチャを compile-time 検証する。

## 次段階

この変更の Debug / Release テスト後、単一入力・単一出力で構造が近い次の processor を優先します。

- `D3D12ColorAdjuster`
- `D3D12KernelFilter`
- `D3D12RegionEffectProcessor`

Blur、Mask、Threshold、Pyramid 系は scratch resource や複数入力を含むため、その後に個別の state descriptor と lifetime 条件を確認して展開します。
