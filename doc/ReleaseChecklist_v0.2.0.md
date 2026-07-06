# Release Checklist v0.2.0

D3D12Helper v0.2.0 は、Processing Layer の主要拡張 API を D3D11Helper 側と概ね揃えるための安定版です。

---

## 1. 事前確認

```bat
git status
```

意図しない変更がないことを確認します。

---

## 2. build / test

```bat
build_test_and_push_if_passed.cmd
```

または手動で実行します。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

---

## 3. version

`CMakeLists.txt` が次であることを確認します。

```cmake
project(D3D12Helper
    VERSION 0.2.0
    LANGUAGES CXX
)
```

---

## 4. commit

```bat
git add CMakeLists.txt .gitignore README.md doc/D3D12Processing.md doc/D3D12ProcessingFutureWork.md doc/ReleaseChecklist_v0.2.0.md
git commit -m "Document D3D12 Processing v0.2.0"
git push
```

---

## 5. tag

```bat
git tag -a v0.2.0 -m "D3D12Helper v0.2.0"
git push origin v0.2.0
```

---

## 6. v0.2.0 scope

### Core / Framework

- D3D12Core
- Queue / Fence / CommandContext
- Resource helper
- Descriptor helper
- Upload / Readback
- Compute / Graphics pipeline
- Shader compiler
- Shared resource

### Processing

- FormatConvert
- Resize
- Remap
- Composite
- Fused Convert + Resize
- Blur
- RegionEffect
- RegionBlur
- ColorAdjust
- KernelFilter
- MaskProcessor
- Threshold / Visualization
- PyramidProcessor
- PyramidBlur
- PyramidRegionBlur

---

## 7. future work

Future work は `doc/D3D12ProcessingFutureWork.md` にまとめています。
