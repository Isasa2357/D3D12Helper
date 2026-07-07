# D3D12Helper v1.9.2 Release Notes

## Summary

v1.9.2 hardens shader asset namespacing for applications that use D3D11Helper and D3D12Helper together.

## Changed

- Runtime sample/test shader copies now place assets under `D3D12Helper/shaders/...`.
- `D3D12ProcessingContext` now prefers `D3D12Helper/shaders/D3D12Processing` as the default shader directory.
- The legacy `shaders/D3D12Processing` path remains as a fallback.
- `D3D12HelperConfig.cmake` now exposes `D3D12Helper_PROCESSING_SHADER_DIR`.
- Package smoke tests validate namespaced installed shader variables.

## Rationale

D3D11Helper and D3D12Helper may contain HLSL files with the same names. Keeping each helper's shader assets below a helper-specific root avoids collisions when both helpers are used by the same application or copied into the same runtime directory.
