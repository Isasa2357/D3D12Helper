# D3D12Helper テスト

機能ごとに分割したunit / integration testです。GoogleTestやCatch2には依存せず、`TestFramework.hpp`の軽量harnessで動作します。

すべての`test_*.cpp`は1つの`d3d12helper_tests`へlinkされ、CTestからsuite名を引数として実行されます。

## 主なsuite

### Core / Foundation

| suite | 主な内容 | Device |
| --- | --- | --- |
| `ModuleHeaders` | 公開umbrella header | 不要 |
| `FormatUtil` | DXGI format判定 | 不要 |
| `DxgiUtil` | LUID utility | 不要 |
| `ThrowIfFailed` | HRESULT例外化 | 不要 |
| `Barrier` | Barrier helper / batch | 一部不要 |
| `Subresource` | subresource index | 不要 |
| `Core` | Device / Adapter / Queue | 必要 |
| `Fence` | Signal / Wait | 必要 |
| `SharedFence` | Shared Fence | 必要 |
| `CommandContext` | Reset / Close lifecycle | 必要 |
| `TypedCommandList` | typed allocator / list / specialized IID | 必要 |

### GPU / Resource

| suite | 主な内容 | Device |
| --- | --- | --- |
| `DescriptorAllocator` | descriptor確保・容量検証 | 必要 |
| `Resource` | Buffer / Texture / state | 必要 |
| `ResourceCreateValidation` | detailed create descriptor / Texture2D validation | 必要 |
| `ResourceView` | 非所有view / COM参照数 / Processing record | 必要 / DXC |
| `Helpers` | SRV / UAV / CBV / RTV / DSV | 必要 |
| `UploadReadback` | CPU→GPU→CPU往復 | 必要 |
| `Transfer` | texture transfer | 必要 |
| `UploadRing` | ring確保・回収 | 必要 |
| `ShaderCompiler` | DXC / D3DCompile | DXC |
| `ShaderReflection` | compiled shader reflection | DXC |
| `ComputePipeline` | Compute PSO / dispatch | DXC |
| `GraphicsPipeline` | Graphics PSO / offscreen draw | DXC |
| `CopyResolveMipmap` | Copy / Resolve / Mipmap | 必要 |
| `ViewState` | view descriptor / state helper | 必要 |
| `Binding` | descriptor/root binding | 必要 |

### Processing

| suite | 主な内容 |
| --- | --- |
| `Processing` | format convert / resize / remap / composite / fused基本経路 |
| `YuvHlslPrimitives` | NV12/P010、matrix/range、GPU golden、plane storage |
| `ProcessingBlur` | Gaussian / Box blur |
| `ProcessingRegionEffect` | region effect |
| `ProcessingRegionBlur` | region blur |
| `ProcessingColorAdjust` | color adjustment |
| `ProcessingKernelFilter` | 3x3 kernel |
| `ProcessingMask` | mask apply / blend / combine / invert |
| `ProcessingThreshold` | threshold / heatmap / color map / overlay |
| `ProcessingPyramid` | downsample / upsample |
| `ProcessingPyramidBlur` | pyramid blur |
| `ProcessingPyramidRegionBlur` | pyramid region blur |
| `AdvancedProcessing` | affine / perspective / 3D LUT / undistort map |

### Compatibility / Hardening

- `Hardening`
- `CompatibilityV1121`
- `CoverageHardening`

`CompatibilityV1121`は、既存public methodやfree functionの型をcompile-timeで固定し、同名overloadによる`decltype(&Function)`の曖昧化も検出します。

## 共通ファイル

- `TestFramework.hpp`: `TEST`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `CHECK_THROWS`, `CHECK_NOTHROW`, `TEST_SKIP`, `TEST_FAIL`。
- `TestCommon.hpp`: `REQUIRE_CORE(var)`、`REQUIRE_DXC()`などの環境依存test helper。
- `test_main.cpp`: 引数なしで全suite、suite名指定で対象suiteのみ実行。

## Buildと実行

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_TESTS=ON ^
  -DD3D12HELPER_BUILD_SAMPLES=OFF ^
  -DD3D12HELPER_INSTALL=OFF ^
  -DD3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS=OFF

cmake --build out/build/default --config Debug --parallel
ctest --test-dir out/build/default -C Debug --parallel --output-on-failure
```

特定suiteのみ:

```bat
ctest --test-dir out/build/default -C Debug -R "^(ResourceView|TypedCommandList|YuvHlslPrimitives)$" --parallel --output-on-failure
```

実行ファイルを直接呼ぶ場合:

```bat
out\build\default\test\Debug\d3d12helper_tests.exe
out\build\default\test\Debug\d3d12helper_tests.exe YuvHlslPrimitives
```

## Package smoke test

Release前にはinstall / `find_package`経路も確認します。

```bat
cmake -S . -B out/build/release -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_TESTS=ON ^
  -DD3D12HELPER_INSTALL=ON ^
  -DD3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS=ON

cmake --build out/build/release --config Release --parallel
ctest --test-dir out/build/release -C Release -R "^PackageSmoke$" --parallel --output-on-failure
```

## 環境によるSKIP

- Hardware GPUがない場合はWARPを使用します。Device自体を作れない場合、Device依存caseはSKIPになります。
- `dxcompiler.dll`がない場合、DXC依存caseはSKIPまたはcompile failureになります。
- Adapterが特定Formatのtyped UAV storeやNV12/P010 plane viewをサポートしない場合、そのcapabilityだけを必要とするcaseがSKIPされます。

## テスト追加方法

1. `test_<Feature>.cpp`を追加する。
2. `TEST(<Suite>, <Case>)`を記述する。
3. 新しいsuiteの場合は`test/CMakeLists.txt`の`D3D12HELPER_TEST_SUITES`へ追加する。

`test_*.cpp`はCMake globで自動的にbuild対象へ入ります。
