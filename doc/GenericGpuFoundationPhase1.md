# Generic GPU Foundation Phase 1

この文書は、D3DVideoEncoder などの上位ライブラリから見つかった不足を、用途非依存の D3D12 基盤として追加する Phase 1 の設計境界を記録します。

## 互換性方針

- v1.12.1 で公開済みの型名、関数名、引数、戻り値、ヘッダパスを削除または変更しない。
- 既存関数と同名の overload 追加によって、関数ポインタ取得や overload resolution を壊さない。
- 既存 `D3D12ReadbackBuffer::Map()` / `Unmap()` を残し、従来の呼び出し経路をテストする。
- Video Encode、codec、Media Foundation、NVENC 固有の型や名前を公開 API に追加しない。
- 互換性を維持できない変更が必要になった場合は、その変更をコミットせず設計判断を一度止める。

## 今回追加する機能

### `D3D12MappedReadRange`

`D3D12ReadbackBuffer::MapRead(offset, size)` から返る move-only RAII 型です。

- 指定範囲を `D3D12_RANGE` に反映する。
- 範囲外を Map 前に拒否する。
- `size == 0` は空 range として扱う。
- 破棄時に written range `{0, 0}` で Unmap する。
- 従来の手動 `Map()` と同時には使用できない。

### `D3D12QueueSyncPoint`

Signal 済み Fence と値をまとめる値型です。

- Fence は `ComPtr` で保持する。
- `D3D12Queue::SignalPoint()` で生成する。
- `GpuWaitPoint()` で別 Queue の GPU wait を積む。
- `CpuWaitPoint()` で CPU 側から完了を待つ。
- 既存 `GpuWait(ID3D12Fence*, UINT64)` は同名 overload を増やさず、そのまま維持する。

### `D3D12BarrierBatch`

明示的な legacy resource barrier をまとめる集約型です。

- Transition / UAV / Aliasing を混在して保持する。
- `before == after` の Transition は追加しない。
- state tracking や Command List への記録は行わない。
- `Data()` / `Count()` を任意の `ResourceBarrier` 対応 Command List へ渡す。

## 今回含めないもの

- Video Encode / Decode 専用 Context
- codec、GOP、rate control、bitstream、mux
- 自動 Resource State tracker
- Typed Command List 基盤
- 外部 Resource view
- 詳細 Resource descriptor API
- YUV420 HLSL primitive の追加整理

これらは Phase 1 のビルド・互換性確認後に、独立した変更として追加します。
