# D3D12Helper ドキュメント

D3D12Helperは、Direct3D 12の定型処理を薄くラップしたC++17ヘルパライブラリです。

v1.13.0では、範囲指定Readback、Queue Sync Point、詳細Resource生成・検証、非所有Resource View、typed command-list基盤、再利用可能なNV12/P010 HLSL primitiveを追加しました。既存の`D3D12Framework`はv1.x互換wrapperとして維持します。

```text
D3D12Foundation
D3D12Core
D3D12Gpu
D3D12Presentation
D3D12Processing
D3D12Interop
D3D12Diagnostics
```

- すべての型・関数は名前空間`D3D12CoreLib`に属します。
- Processing Layerは`D3D12CoreLib::Processing`に属します。
- 対象環境はWindows / MSVC、Direct3D 12 + DXGI 1.6です。
- ライブラリ本体は`include/`と`src/`、Processing shaderは`shaders/`にあります。

## ドキュメントindex

| ファイル | 内容 |
| --- | --- |
| `README.md`（本書） | 全体像・設計思想・組み込み方法 |
| [`Architecture.md`](Architecture.md) | 公開module構成と依存方向 |
| [`D3D12Foundation.md`](D3D12Foundation.md) | DirectX / DXGI基礎utility |
| [`D3D12Core.md`](D3D12Core.md) | Device / Queue / Fence / Command API |
| [`D3D12Gpu.md`](D3D12Gpu.md) | Resource / Descriptor / Transfer / Pipeline / Validation |
| [`D3D12ShaderReflection.md`](D3D12ShaderReflection.md) | Shader reflection |
| [`D3D12Framework.md`](D3D12Framework.md) | v1.x互換Framework API |
| [`D3D12Presentation.md`](D3D12Presentation.md) | SwapChain / Present |
| [`D3D12Processing.md`](D3D12Processing.md) | Processing API |
| [`D3D12ProcessingWorkflow.md`](D3D12ProcessingWorkflow.md) | HLSL library / custom fused shader workflow |
| [`D3D12Interop.md`](D3D12Interop.md) | Shared Resource / Shared Fence |
| [`D3D12Diagnostics.md`](D3D12Diagnostics.md) | Debug Layer / InfoQueue / DRED |
| [`Packaging.md`](Packaging.md) | install / FetchContent / `find_package` |
| [`Patterns.md`](Patterns.md) | よくある処理pattern |
| [`TestCoverage.md`](TestCoverage.md) | CTest coverage |
| [`GenericGpuFoundationPhase1.md`](GenericGpuFoundationPhase1.md) | Readback / Sync / Resource / Validation / Barrier設計 |
| [`GenericGpuFoundationPhase2Audit.md`](GenericGpuFoundationPhase2Audit.md) | 非所有Resource View監査 |
| [`GenericGpuFoundationPhase3TypedCommandList.md`](GenericGpuFoundationPhase3TypedCommandList.md) | Typed Command List設計 |
| [`GenericGpuFoundationPhase4YuvHlslPrimitives.md`](GenericGpuFoundationPhase4YuvHlslPrimitives.md) | YUV HLSL primitive設計 |
| [`GenericGpuFoundationFinalAudit.md`](GenericGpuFoundationFinalAudit.md) | Phase 1～4最終監査 |
| [`ReleaseChecklist_v1.13.0.md`](ReleaseChecklist_v1.13.0.md) | v1.13.0 release前確認 |
| [`ReleaseNotes_v1.13.0.md`](ReleaseNotes_v1.13.0.md) | v1.13.0 release notes |
| [`../sample/README.md`](../sample/README.md) | サンプル一覧 |
| [`../test/README.md`](../test/README.md) | test構成と実行方法 |

過去のrelease notesも`doc/ReleaseNotes_v*.md`として保持しています。

---

## 設計思想

### 公開moduleと依存方向

```text
D3D12Foundation
  DirectX / DXGI / HRESULT / format utility

D3D12Core
  device / adapter / queue / fence / allocator / command list / barrier

D3D12Gpu
  resource / descriptor / upload / readback / pipeline / validation

D3D12Presentation
  swap-chain and presentation helpers

D3D12Processing
  GPU image processing and reusable HLSL primitives

D3D12Interop
  shared resource and shared fence helpers

D3D12Diagnostics
  debug layer / InfoQueue / DRED / object naming
```

```text
Layer 1 : D3D12Core
Layer 2 : D3D12Framework -> D3D12Gpu / D3D12Presentation / D3D12Interopへ分類
Layer 3 : D3D12Processing
```

`D3D12Framework`はv1.xでは削除しません。新規コードでは目的別moduleを優先できます。

### 1つのCoreを共有

`D3D12Core::CreateShared()`が返す`std::shared_ptr<D3D12Core>`をWindow、Camera、Renderer、Processingなどで共有する想定です。Descriptor Allocatorはsubsystemごとに必要な数を作成します。

### 明示stateと非所有view

- `D3D12Resource`はResource全体に対する簡易stateを1つ保持します。
- subresource単位、複数Queue、外部API共有ではbefore / afterを明示してください。
- `D3D12ResourceView`は`AddRef` / `Release`を行いません。
- Viewを使ったGPU処理が完了するまで、外部ownerがResource lifetimeを保証します。

### Allocator再利用

`D3D12CommandAllocatorContext::Reset()`と`D3D12CommandContext::Reset()`はGPU待機を行いません。関連するGPU処理がFence等で完了した後にResetしてください。

### 例外ベースのerror処理

失敗したHRESULTは`D3D12CORE_THROW_IF_FAILED()`で例外化されます。Processingではvalidation error、unsupported format、unsupported featureも例外で通知します。

---

## 組み込み方法

CMake:

```cmake
set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

Visual Studioへ直接追加する場合:

1. include pathに`include/`と`include/D3D12Helper/`を追加する。
2. `src/*.cpp`をcompile対象へ追加する。
3. DXC利用時は`dxcompiler.dll` / `dxil.dll`を配置する。
4. Processing利用時は`D3D12Helper/shaders/D3D12Processing/`を配置する。

推奨include:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```

互換include:

```cpp
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
```

実行可能な例は[`../sample`](../sample)、test実行方法は[`../test/README.md`](../test/README.md)を参照してください。
