# Generic GPU Foundation Phase 3: Typed Command Lists

Phase 3 separates command-allocator ownership from command-list-interface selection. The foundation is intentionally unaware of graphics, video encode, video decode, or any other subsystem-specific recording operations.

## Public API

Header:

```cpp
#include <D3D12Helper/D3D12Core/D3D12CommandAllocatorContext.hpp>
```

Allocator ownership:

```cpp
D3D12CommandAllocatorContext allocator;
allocator.Initialize(device, D3D12_COMMAND_LIST_TYPE_DIRECT);

ID3D12CommandAllocator* raw = allocator.GetAllocator();
D3D12_COMMAND_LIST_TYPE type = allocator.GetType();
```

Typed command-list creation:

```cpp
auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
    device,
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    allocator);
```

`CreateTypedCommandList` returns the command list in the initial open/recording state produced by `ID3D12Device::CreateCommandList`. Closing, resetting, recording, submission, and command-list-specific methods remain the caller's responsibility.

## Allocator reset safety

`D3D12CommandAllocatorContext::Reset()` does not wait for the GPU. Before calling it, the caller must guarantee with a fence or equivalent synchronization that every submitted command list recorded with that allocator has completed.

```cpp
D3D12CORE_THROW_IF_FAILED(list->Close());
ID3D12CommandList* lists[] = { list.Get() };
queue.ExecuteCommandLists(1, lists);
queue.WaitForFenceValue(queue.Signal());

allocator.Reset();
```

Resetting an allocator while GPU work still references it is invalid D3D12 usage.

## Type validation

The context overload checks that the requested list type matches the allocator context type before entering D3D12:

```cpp
CreateTypedCommandList<ID3D12GraphicsCommandList>(
    device,
    D3D12_COMMAND_LIST_TYPE_COPY,
    directAllocator); // throws before CreateCommandList
```

The raw-allocator overload cannot inspect the allocator's creation type because D3D12 exposes no type query on `ID3D12CommandAllocator`. For that overload, a mismatch is reported by `ID3D12Device::CreateCommandList`.

## Specialized SDK command-list interfaces

D3D12Helper does not include or require `d3d12video.h`. An upper library may include the SDK header and instantiate the same template for a specialized interface:

```cpp
#include <d3d12video.h>
#include <D3D12Helper/D3D12Core/D3D12CommandAllocatorContext.hpp>

D3D12CommandAllocatorContext allocator;
allocator.Initialize(device, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE);

auto decodeList = CreateTypedCommandList<ID3D12VideoDecodeCommandList>(
    device,
    D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
    allocator);
```

Video/decode-specific commands, codec structures, and queue policy remain in the upper library.

## Existing `D3D12CommandContext`

`D3D12CommandContext` keeps its existing public API and source compatibility. Internally it now uses `D3D12CommandAllocatorContext` and `CreateTypedCommandList<ID3D12GraphicsCommandList>`.

## Minimal sample

`sample/19_TypedCommandList/main.cpp` creates a Direct allocator and typed graphics command list, submits an empty list, waits with a fence, and safely resets the allocator.

## Tests

The `TypedCommandList` suite covers:

- uninitialized and initialized allocator lifecycle,
- Direct, Compute, and Copy command-list creation,
- raw-allocator and allocator-context overloads,
- pre-D3D type-mismatch rejection,
- null argument rejection,
- allocator reuse after fence completion,
- compile-time compatibility with SDK-specialized interfaces when `d3d12video.h` is available.

The `CompatibilityV1121` suite verifies that every existing `D3D12CommandContext` public signature remains unchanged and unambiguous.
