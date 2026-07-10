# D3D12Helper v1.13.0 Release Checklist

## Release scope

v1.13.0 contains the generic GPU foundation merged through PRs #25–#28:

- range-specific readback mapping,
- queue synchronization points,
- detailed resource creation and validation,
- non-owning resource views and Processing `*View` paths,
- barrier aggregation,
- typed command allocator/list foundation,
- reusable NV12/P010 YUV HLSL primitives.

## Source checks

- [ ] `CMakeLists.txt` declares `VERSION 1.13.0`.
- [ ] Top-level `README.md` identifies v1.13.0 as the stable version.
- [ ] `doc/ReleaseNotes_v1.13.0.md` is present.
- [ ] `doc/GenericGpuFoundationFinalAudit.md` is present.
- [ ] Public v1.x names and include paths remain unchanged.
- [ ] `CompatibilityV1121` passes.

## Configure

Use a clean build directory and enable install/package validation.

```bat
cmake -S . -B out/build/release-v1.13.0 -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=ON ^
  -DD3D12HELPER_BUILD_TESTS=ON ^
  -DD3D12HELPER_INSTALL=ON ^
  -DD3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS=ON
```

## Debug validation

- [ ] Debug build passes with `--parallel`.
- [ ] Full Debug CTest passes with `--parallel`.
- [ ] `CompatibilityV1121` passes.
- [ ] `ResourceCreateValidation` passes.
- [ ] `ResourceView` passes.
- [ ] `TypedCommandList` passes.
- [ ] `YuvHlslPrimitives` passes.

## Release validation

- [ ] Release build passes with `--parallel`.
- [ ] Full Release CTest passes with `--parallel`.
- [ ] `PackageSmoke` passes.
- [ ] Install step succeeds.
- [ ] Installed package reports version 1.13.0.

## Sample validation

Run at least:

- [ ] `D3D12Sample_01_HelloDevice`
- [ ] `D3D12Sample_02_ComputeGrayscale`
- [ ] `D3D12Sample_07_ProcessingFusedConvertResize`
- [ ] `D3D12Sample_08_ProcessingP010Rgba16`
- [ ] `D3D12Sample_18_ProcessingCustomFusedShader`
- [ ] `D3D12Sample_19_TypedCommandList`

`03_HelloTriangle` is optional for headless release validation because it requires a GUI.

## Repository actions

After all checks pass:

1. Mark the release PR ready.
2. Merge the release PR into `main`.
3. Confirm `main` is at the expected release commit.
4. Create annotated tag `v1.13.0` on that commit.
5. Push the tag.
6. Create the GitHub Release using `ReleaseNotes_v1.13.0.md`.
7. Do not attach compiler/runtime DLLs unless redistribution terms have been reviewed.
8. Delete merged release/feature branches when no longer needed.

## Post-release verification

- [ ] Clone or fetch from a clean directory.
- [ ] Checkout tag `v1.13.0`.
- [ ] Configure and run `PackageSmoke` once from the tag.
- [ ] Confirm README and release notes render correctly on GitHub.
