# D3D12Helper v1.10.0 Release Notes

## Summary

v1.10.0 adds lightweight shader reflection helpers.

## Added

- `D3D12ShaderReflection.hpp`.
- `ReflectShaderBytecode` for compiled DXBC bytecode.
- Resource binding inspection.
- Constant buffer and variable inspection.
- Input and output signature parameter inspection.
- Input layout element generation from vertex shader signatures.
- Compute shader thread group size inspection.
- Shader reflection tests.

## Notes

The initial implementation focuses on bytecode produced by `D3DCompile`. DXIL/DXC container reflection can be added as a later hardening step without changing the high-level API shape.
