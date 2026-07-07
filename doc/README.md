# D3D12Helper ドキュメント

D3D12Helper は、Direct3D 12 の定型処理を薄くラップした C++17 ヘルパライブラリです。

v1.1.0 では、D3D11Helper と同じ公開モジュール分類を導入しています。既存の `D3D12Framework` は v1.x 互換 wrapper として維持します。

```text
D3D12Foundation
D3D12Core
D3D12Gpu
D3D12Presentation
D3D12Processing
D3D12Interop
D3D12Diagnostics
```

- すべての型・関数は名前空間 `D3D12CoreLib` に属します。
- Processing Layer の型・関数は `D3D12CoreLib::Processing` に属します。
- 対象環境は Windows / MSVC、Direct3D 12 + DXGI 1.6 です。
- ライブラリ本体は `include/` と `src/` に分かれます。

ドキュメントは次のファイルに分かれています。

| ファイル | 内容 |
| --- | --- |
| `README.md`（本書） | 全体像・設計思想・ファイル構成・組み込み方法・クイックスタート |
| [`Architecture.md`](Architecture.md) | v1.1.0 以降の公開モジュール構成と依存方向 |
| [`D3D12Foundation.md`](D3D12Foundation.md) | DirectX/DXGI-only の基礎 utility |
| [`D3D12Core.md`](D3D12Core.md) | Layer 1 / Core API リファレンス |
| [`D3D12Gpu.md`](D3D12Gpu.md) | Resource / Descriptor / Upload / Readback / Pipeline / ShaderCompiler |
| [`D3D12Framework.md`](D3D12Framework.md) | v1.x 互換 Framework API リファレンス |
| [`D3D12Presentation.md`](D3D12Presentation.md) | SwapChain / presentation helper |
| [`D3D12Processing.md`](D3D12Processing.md) | Processing API リファレンス |
| [`D3D12Interop.md`](D3D12Interop.md) | shared resource / shared fence interop |
| [`D3D12Diagnostics.md`](D3D12Diagnostics.md) | debug layer / InfoQueue / DRED / object naming |
| [`D3D12ProcessingFutureWork.md`](D3D12ProcessingFutureWork.md) | Processing Layer の future work |
| [`Packaging.md`](Packaging.md) | install / FetchContent / find_package |
| [`TestCoverage.md`](TestCoverage.md) | CTest suite coverage |
| [`ReleaseNotes_v1.0.0.md`](ReleaseNotes_v1.0.0.md) | v1.0.0 release notes |
| [`ReleaseNotes_v1.1.0.md`](ReleaseNotes_v1.1.0.md) | v1.1.0 release notes |
| [`ReleaseNotes_v1.8.1.md`](ReleaseNotes_v1.8.1.md) | v1.8.1 release notes |
| [`ReleaseNotes_v1.9.0.md`](ReleaseNotes_v1.9.0.md) | v1.9.0 release notes |
| [`ReleaseNotes_v1.9.1.md`](ReleaseNotes_v1.9.1.md) | v1.9.1 release notes |
| [`Patterns.md`](Patterns.md) | よくある処理パターン（レシピ集） |

実際に動くコードは [`../sample`](../sample) を参照してください。

---

## 設計思想

### v1.1.0+ 公開モジュール構成

```text
D3D12Foundation
  DirectX / DXGI / HRESULT / format utility

D3D12Core
  device / adapter / queue / fence / command context / barrier

D3D12Gpu
  resource / descriptor / upload / readback / pipeline / shader compiler / view helpers

D3D12Presentation
  swap-chain and presentation helpers

D3D12Processing
  GPU image processing

D3D12Interop
  shared resource and shared fence helpers

D3D12Diagnostics
  debug layer / InfoQueue / DRED / object naming
```

### 既存 Layer との対応

```text
Layer 1 : D3D12Core
Layer 2 : D3D12Framework  -> v1.1.0 以降は D3D12Gpu / D3D12Presentation / D3D12Interop へ分類
Layer 3 : D3D12Processing
```

`D3D12Framework` は v1.x では削除しません。新規コードでは、目的に応じて `D3D12Gpu` / `D3D12Presentation` / `D3D12Interop` を優先して include できます。

### 共有方針：1 つの Core を全サブシステムで共有

`D3D12Core::CreateShared()` は `std::shared_ptr<D3D12Core>` を返します。Window・Camera・Renderer・Processing といったサブシステムは、この 1 つの Core を共有保持して使う想定です。Descriptor Allocator だけはサブシステムごとに必要なだけ生成します（Core は所有しません）。

### 明示的バリア・単一状態追跡

- リソース遷移は基本「明示的」です。`MakeTransitionBarrier()` などでバリアを作り、`D3D12CommandContext::ResourceBarrier()` で積みます。
- `D3D12Resource` は「現在の状態」を **1 つだけ** 追跡する簡易ラッパです。サブリソースごとに状態が分かれるケース（mip / array / planar）、複数キュー同時参照、外部 API 共有では、必要に応じて before/after を明示して手動管理してください。

### 例外ベースのエラー処理

失敗した `HRESULT` は `D3D12CORE_THROW_IF_FAILED()` で例外化されます。Processing Layer では validation error / unsupported format / unsupported feature も例外として通知します。

---

## ディレクトリ構成

```text
D3D12Helper/
├─ include/D3D12Helper/
│  ├─ D3D12Foundation/
│  ├─ D3D12Core/
│  ├─ D3D12Gpu/
│  ├─ D3D12Framework/
│  ├─ D3D12Presentation/
│  ├─ D3D12Processing/
│  ├─ D3D12Interop/
│  └─ D3D12Diagnostics/
├─ shaders/D3D12Processing/
├─ src/
├─ doc/
├─ sample/
├─ test/
└─ CMakeLists.txt
```

---

## 組み込み方法

このライブラリは **ヘッダ + .cpp + HLSL shader** のソース構成です。CMake なら `add_subdirectory` で組み込めます。

```cmake
add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

CMake を使わず Visual Studio プロジェクトに直接追加する場合は、次を追加します。

1. インクルードパスに `include/` と `include/D3D12Helper/` を通す。
2. `src/*.cpp` をコンパイル対象に追加する。
3. DXC を使う場合は `dxcompiler.dll` / `dxil.dll` を実行ファイル横または PATH 上に置く。
4. Processing Layer を使う場合は `shaders/D3D12Processing/` を実行時に参照できる場所へ配置する。

推奨 include:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```

互換 include:

```cpp
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
```

---

## クイックスタート

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <iostream>

using namespace D3D12CoreLib;

int main() {
    try {
        D3D12CoreConfig config;
        config.allowWarpAdapter = true;
        auto core = D3D12Core::CreateShared(config);

        std::wcout << L"Adapter: "
                   << core->DeviceContext().GetAdapterName() << L"\n";

        D3D12Resource buffer = CreateBuffer(
            *core,
            1024,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON);

        core->WaitIdle();
    } catch (const std::exception& e) {
        std::cerr << "D3D12 init failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

GPU で Compute を回して結果を CPU に戻す例は [`../sample/02_ComputeGrayscale`](../sample/02_ComputeGrayscale) と [`Patterns.md`](Patterns.md) を参照してください。Processing Layer の例は [`D3D12Processing.md`](D3D12Processing.md) と [`../sample`](../sample) を参照してください。
