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
| `test_UploadReadback.cpp` | `UploadReadback` | Upload→GPU→Readback 往復 | 必要 |
| `test_UploadRing.cpp` | `UploadRing` | 確保・会計・回収 | 必要 |
| `test_ShaderCompiler.cpp` | `ShaderCompiler` | DXC/D3DCompile・失敗時例外 | DXC |
| `test_ComputePipeline.cpp` | `ComputePipeline` | テンプレ RootSig の Dispatch 検証 | DXC |
| `test_GraphicsPipeline.cpp` | `GraphicsPipeline` | PipelineDefaults・オフスクリーン描画 | DXC |

共通ファイル:

- `TestFramework.hpp` — 依存なしのハーネス。`TEST(suite, name)`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `CHECK_THROWS`, `CHECK_NOTHROW`, `TEST_SKIP`, `TEST_FAIL`。
- `TestCommon.hpp` — デバイスを使うテスト用。`REQUIRE_CORE(var)`（WARP 許可で Core を作り、作れなければ SKIP）、`REQUIRE_DXC()`（`dxcompiler.dll` が無ければ SKIP）。
- `test_main.cpp` — 引数なしで全 suite、引数に suite 名を渡すとその機能だけ実行。

すべての `test_*.cpp` は 1 つの実行ファイル `d3d12helper_tests` にまとめられ、CTest からは **suite 名を引数に渡して機能単位で** 実行します。

## ビルドと実行

ルートからビルドすると、既定でテストも一緒にビルドされます（`D3D12HELPER_BUILD_TESTS=ON`）。

```bat
cmake -S . -B build -A x64
cmake --build build --config Release

:: CTest で機能単位に実行
ctest --test-dir build -C Release --output-on-failure

:: 特定機能だけ
ctest --test-dir build -C Release -R ComputePipeline --output-on-failure
```

実行ファイルを直接呼ぶこともできます。

```bat
:: 全 suite
build\test\Release\d3d12helper_tests.exe

:: 特定 suite だけ
build\test\Release\d3d12helper_tests.exe FormatUtil
build\test\Release\d3d12helper_tests.exe ComputePipeline
```

## 環境による SKIP

- GPU が無い環境でも WARP で動きますが、D3D12 デバイスをまったく作れない場合、デバイス依存の suite は各ケースが `[ SKIP ]` になります（失敗ではありません）。
- `dxcompiler.dll` が見つからない場合、DXC を使う suite（ShaderCompiler / ComputePipeline / GraphicsPipeline）は SKIP されます。

## テストの追加

新しい機能のテストは `test_<Feature>.cpp` を追加し、`TEST(<Feature>, <Case>) { ... }` を書くだけです（自己登録されます）。`test/CMakeLists.txt` の `D3D12HELPER_TEST_SUITES` に `<Feature>` を足すと、その機能が独立した CTest エントリになります。ファイルは `test_*.cpp` glob で自動的にビルド対象へ入ります。
