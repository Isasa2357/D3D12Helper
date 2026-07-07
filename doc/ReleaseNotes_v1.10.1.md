# D3D12Helper v1.10.1 Release Notes

## Summary

v1.10.1 hardens shader reflection by supporting DXIL/container bytecode produced by DXC.

## Changed

- `ReflectShaderBytecode` now first tries `D3DReflect` for DXBC bytecode.
- If `D3DReflect` fails, reflection falls back to `IDxcUtils::CreateReflection` for DXIL/container bytecode.
- Added a DXC/DXIL reflection test. The test is skipped when DXC is unavailable.

## Notes

DXIL reflection requires `dxcompiler.dll` to be available at runtime.
