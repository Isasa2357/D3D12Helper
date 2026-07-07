# D3D12Helper v1.1.0 Release Notes

## Summary

v1.1.0 adds the D3D11Helper-style public module layout to D3D12Helper.

This is a non-breaking organization release. Existing D3D12Core, D3D12Framework, and D3D12Processing include paths remain valid.

## Added modules

- D3D12Foundation
- D3D12Gpu
- D3D12Presentation
- D3D12Interop
- D3D12Diagnostics

## Compatibility

D3D12Framework remains available as a v1.x compatibility wrapper.

New code should prefer the category headers when possible:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```

## Validation

v1.1.0 adds a ModuleHeaders smoke test suite to ensure the new umbrella headers compile together.
