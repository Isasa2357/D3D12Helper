# D3D12Helper Architecture

v1.1.0 introduces the public module layout used by D3D11Helper.

```text
D3D12Foundation
D3D12Core
D3D12Gpu
D3D12Presentation
D3D12Processing
D3D12Interop
D3D12Diagnostics
```

The existing implementation is not moved in v1.1.0. This release adds canonical umbrella headers and keeps D3D12Framework as a compatibility wrapper.

## Dependency direction

```text
Foundation
  -> Core
      -> Gpu
          -> Presentation
          -> Processing
          -> Interop
          -> Diagnostics
```

The exact D3D12 dependency shape is not identical to D3D11Helper because D3D12 has explicit command queues, resource states, descriptor heaps, and fences.

## Compatibility

Existing include paths remain valid:

```cpp
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>
```

New code can use category headers:

```cpp
#include <D3D12Helper/D3D12Foundation/D3D12Foundation.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Presentation/D3D12Presentation.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>
#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>
```
