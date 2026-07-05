# D3D12Helper テスト

機能（suite）ごとにファイルを分割したユニット/統合テストです。外部のテストフレームワーク（GoogleTest / Catch2 等）に依存せず、`TestFramework.hpp` の軽量ハーネスで動きます。

## 構成

| ファイル | suite | 内容 | デバイス |
| --- | --- | --- | --- |
| `test_FormatUtil.cpp` | `FormatUtil` | フォーマット判定・bpp/Bpp | 不要 |
| `test_DxgiUtil.cpp` | `DxgiUtil` | LUID 比較・文字列化 | 不要 |
| `test_ThrowIfFailed.cpp` | `ThrowIfFailed` | HRESULT 例外化・マクロ | 不要 |
| `test_Barrier.cpp` | `Barrier` | バリア生成ヘルパのフィールド | 不要 |
| `test_Core.cpp` | `Core` | 初期化・アダプタ・キュー | 必要 |
| `test_Fence.cpp` | `Fence` | Signal/Wait・完了値 | 必要 |
| `test_CommandContext.cpp` | `CommandContext` | Reset/Close ライフサイクル | 必要 |
| `test_DescriptorAllocator.cpp` | `DescriptorAllocator` | 確保・容量超過・RTV | 必要 |
| `test_Resource.cpp` | `Resource` | バッファ/テクスチャ生成・状態追跡 | 必要 |
| `test_HelperViews.cpp` / `test_Helpers.cpp` | `Helpers` | Buffer SRV/UAV、CBV、RTV、DSV、upload helper | 必要 |
| `test_UploadReadback.cpp` | `UploadReadback` | Upload→GPU→Readback 往復 | 必要 |
| `test_UploadRing.cpp` | `UploadRing` | 確保・会計・回収 | 必要 |
| `test_ShaderCompiler.cpp` | `ShaderCompiler` | DXC/D3DCompile・失敗時例外 | DXC |
| `test_ComputePipeline.cpp` | `ComputePipeline` | テンプレ RootSig の Dispatch 検証 | DXC |
| `test_GraphicsPipeline.cpp` | `GraphicsPipeline` | PipelineDefaults・オフスクリーン描画 | DXC |
| `test_Processing.cpp` | `Processing` | Processing 基本 API、format convert、resize、NV12 readback | 必要 / DXC |
| `test_ProcessingRemapComposite.cpp` | `Processing` | remap / composite の shader compile と readback 検証 | 必要 / DXC |
| `test_ProcessingFusedYuvP010.cpp` | `Processing` | fused convert+resize、P010、RGBA16F の readback 検証 | 必要 / DXC |

共通ファイル:

- `TestFramework.hpp` — 依存なしのハーネス。`TEST(suite, name)`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `CHECK_THROWS`, `CHECK_NOTHROW`, `TEST_SKIP`, `TEST_FAIL`。
- `TestCommon.hpp` — デバイスを使うテスト用。`REQUIRE_CORE(var)`（WARP 許可で Core を作り、作れなければ SKIP）、`REQUIRE_DXC()`（`dxcompiler.dll` が無ければ SKIP）。
- `test_main.cpp` — 引数なしで全 suite、引数に suite 名を渡すとその機能だけ実行。

すべての `test_*.cpp` は 1 つの実行ファイル `d3d12helper_tests` にまとめられ、CTest からは **suite 名を引数に渡して機能単位で** 実行します。

## Processing suite の検証内容

`Processing` suite は Layer 3 の GPU 実行結果を readback して検証します。

主な検証:

- `D3D12ProcessingContext` の初期化と capability query
- shader cache による Processing shader compile
- RGBA / BGRA / RGBA16F / NV12 / P010 view 作成
- RGBA copy / BGRA typed store の readback 検証
- NV12 → RGBA の readback 検証
- RGBA → NV12 の Y / UV plane readback 検証
- P010 → RGBA の readback 検証
- point resize の readback 検証
- remap の readback 検証
- composite alpha blend の readback 検証
- fused convert+resize の readback 検証

## ビルドと実行

ルートからビルドすると、既定でテストも一緒にビルドされます（`D3D12HELPER_BUILD_TESTS=ON`）。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

特定機能だけ実行する場合:

```bat
ctest --test-dir out/build/default -C Debug -R Processing --output-on-failure
ctest --test-dir out/build/default -C Debug -R ShaderCompiler --output-on-failure
```

実行ファイルを直接呼ぶこともできます。

```bat
out\build\default\test\Debug\d3d12helper_tests.exe
out\build\default\test\Debug\d3d12helper_tests.exe Processing
```

## 環境による SKIP

- GPU が無い環境でも WARP で動きますが、D3D12 デバイスをまったく作れない場合、デバイス依存の suite は各ケースが `[ SKIP ]` になります（失敗ではありません）。
- `dxcompiler.dll` が見つからない場合、DXC を使う suite（ShaderCompiler / ComputePipeline / GraphicsPipeline / Processing）は SKIP または compile 失敗になります。
- 一部の typed UAV store が非対応な環境では、対応 format を必要とする Processing test が SKIP されることがあります。

## テストの追加

新しい機能のテストは `test_<Feature>.cpp` を追加し、`TEST(<Feature>, <Case>) { ... }` を書くだけです（自己登録されます）。`test/CMakeLists.txt` の `D3D12HELPER_TEST_SUITES` に `<Feature>` を足すと、その機能が独立した CTest エントリになります。ファイルは `test_*.cpp` glob で自動的にビルド対象へ入ります。
