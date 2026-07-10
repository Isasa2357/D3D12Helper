# D3D12Core リファレンス（Layer 1）

`include/D3D12Helper/D3D12Core`に含まれる型・関数の概要です。すべて名前空間`D3D12CoreLib`に属します。

- [D3D12Common](#d3d12common)
- [D3D12CoreConfig](#d3d12coreconfig)
- [D3D12Core](#d3d12core)
- [D3D12DeviceContext](#d3d12devicecontext)
- [D3D12Queue / Sync Point](#d3d12queue--sync-point)
- [D3D12Fence](#d3d12fence)
- [D3D12CommandAllocatorContext](#d3d12commandallocatorcontext)
- [CreateTypedCommandList](#createtypedcommandlist)
- [D3D12CommandContext](#d3d12commandcontext)
- [D3D12BarrierBatch](#d3d12barrierbatch)
- [Barrier helper](#barrier-helper)
- [Subresource helper](#subresource-helper)
- [FormatUtil](#formatutil)
- [Debug / ThrowIfFailed](#debug--throwiffailed)

---

## D3D12Common

`D3D12Common.hpp`は`windows.h`、`d3d12.h`、`dxgi1_6.h`、WRL `ComPtr`などを取り込みます。

```cpp
namespace D3D12CoreLib {
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
}
```

---

## D3D12CoreConfig

```cpp
struct D3D12CoreConfig {
    bool enableDebugLayer = true;
    bool enableGpuValidation = false;
    bool enableInfoQueue = true;
    bool enableDred = true;

    bool preferHighPerformanceAdapter = true;
    bool allowWarpAdapter = false;

    bool createDirectQueue = true;
    bool createComputeQueue = false;
    bool createCopyQueue = true;

    bool breakOnError = false;
    bool breakOnCorruption = false;
    bool breakOnWarning = false;
};
```

Direct Queueは常に作成されます。Compute / Copyの専用Queueはconfigで制御します。

---

## D3D12Core

Device / Queue / Command Contextをまとめるfacadeです。Application内では`std::shared_ptr<D3D12Core>`をsubsystem間で共有する想定です。

```cpp
void Initialize(const D3D12CoreConfig& config = {});
void InitializeWithAdapterLuid(LUID luid, const D3D12CoreConfig& config = {});

static std::shared_ptr<D3D12Core> CreateShared(
    const D3D12CoreConfig& config = {});

D3D12DeviceContext& DeviceContext();
D3D12Queue& DirectQueue();
D3D12Queue& CopyQueue();
D3D12Queue* ComputeQueue();

ID3D12Device* GetDevice() const;
LUID GetAdapterLuid() const;
bool IsSameAdapter(LUID other) const;

D3D12CommandContext CreateDirectContext();
D3D12CommandContext CreateCopyContext();
D3D12CommandContext CreateComputeContext();

void WaitIdle();
```

---

## D3D12DeviceContext

DXGI Factory / Adapter / DeviceとAdapter情報を保持します。

```cpp
ID3D12Device* GetDevice() const;
IDXGIFactory6* GetFactory() const;
IDXGIAdapter1* GetAdapter() const;

LUID GetAdapterLuid() const;
std::wstring GetAdapterName() const;

bool SupportsResourceSharing() const;
bool SupportsTypedUavLoad(DXGI_FORMAT format) const;
```

---

## D3D12Queue / Sync Point

`D3D12Queue`は`ID3D12CommandQueue`と専用Fenceを管理します。

```cpp
void Initialize(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    D3D12_COMMAND_QUEUE_PRIORITY priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

ID3D12CommandQueue* Get() const;
D3D12_COMMAND_LIST_TYPE GetType() const;
D3D12Fence& Fence();

void ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists);
UINT64 Signal();
void WaitForFenceValue(UINT64 value);
void WaitIdle();
void GpuWait(ID3D12Fence* fence, UINT64 value);
```

v1.13.0では、Signal済みFenceと値を束ねる`D3D12QueueSyncPoint`を追加しました。

```cpp
D3D12QueueSyncPoint point = producer.SignalPoint();
consumer.GpuWaitPoint(point); // CPUはblockしない。
consumer.CpuWaitPoint(point); // pointのFence完了までCPU wait。
```

```cpp
ID3D12Fence* D3D12QueueSyncPoint::GetFence() const;
UINT64 D3D12QueueSyncPoint::GetValue() const;
bool D3D12QueueSyncPoint::IsValid() const;
```

Sync PointはFenceを`ComPtr`で保持します。既存`GpuWait(ID3D12Fence*, UINT64)`の関数pointer一意性を保つため、同名overloadではなく`GpuWaitPoint`を使用します。

---

## D3D12Fence

```cpp
void Initialize(ID3D12Device* device);
UINT64 Signal(ID3D12CommandQueue* queue);
void Wait(UINT64 fenceValue);
void WaitIdle(ID3D12CommandQueue* queue);

bool IsCompleted(UINT64 fenceValue) const;
UINT64 GetCurrentValue() const;
UINT64 GetCompletedValue() const;
ID3D12Fence* Get() const;
```

---

## D3D12CommandAllocatorContext

Command Allocatorの所有とCommand List interface選択を分離するv1.13.0 APIです。

```cpp
D3D12CommandAllocatorContext allocator;
allocator.Initialize(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);

ID3D12CommandAllocator* raw = allocator.GetAllocator();
D3D12_COMMAND_LIST_TYPE type = allocator.GetType();
bool initialized = allocator.IsInitialized();
```

```cpp
void Reset();
```

`Reset()`はGPU待機を行いません。そのAllocatorで記録したすべての投入済みCommand Listが完了した後に呼び出してください。

---

## CreateTypedCommandList

```cpp
auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
    device,
    D3D12_COMMAND_LIST_TYPE_COMPUTE,
    allocator);
```

次の2経路があります。

```cpp
template<class TCommandList>
ComPtr<TCommandList> CreateTypedCommandList(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    ID3D12CommandAllocator* allocator);

template<class TCommandList>
ComPtr<TCommandList> CreateTypedCommandList(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    const D3D12CommandAllocatorContext& allocatorContext);
```

Context版は要求typeとAllocator typeの不一致をD3D12呼び出し前に拒否します。raw Allocator版はAllocator typeをqueryできないため、D3D12 runtimeの検証へ委ねます。

D3D12Helper本体は`d3d12video.h`に依存しません。上位library側でSDK headerをincludeすれば、同じtemplateを`ID3D12VideoDecodeCommandList`などへinstantiateできます。

---

## D3D12CommandContext

既存のAllocator + Graphics Command List wrapperです。v1.13.0では内部実装を`D3D12CommandAllocatorContext`と`CreateTypedCommandList<ID3D12GraphicsCommandList>`へ共通化しましたが、公開signatureは変更していません。

```cpp
void Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

ID3D12GraphicsCommandList* GetCommandList() const;
ID3D12CommandAllocator* GetAllocator() const;
D3D12_COMMAND_LIST_TYPE GetType() const;
bool IsOpen() const;

void Reset();
void Close();

void ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);
void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers);
```

`Reset()`前にGPU完了をFenceで保証してください。二重`Close()`は無害です。

---

## D3D12BarrierBatch

明示before / afterを持つlegacy Resource Barrierの集約型です。

```cpp
D3D12BarrierBatch batch;
batch.Reserve(4);
batch.Transition(resource, before, after);
batch.Uav(resource);
batch.Aliasing(beforeResource, afterResource);

commandContext.ResourceBarrier(batch.Count(), batch.Data());
batch.Clear();
```

```cpp
bool Transition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

void Uav(ID3D12Resource* resource);
void Aliasing(ID3D12Resource* before, ID3D12Resource* after);
```

- `before == after`のTransitionは追加せず`false`を返す。
- 自動state trackingは行わない。
- Resource state cacheを更新しない。
- Command Listを保持しない。

---

## Barrier helper

```cpp
D3D12_RESOURCE_BARRIER MakeTransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

D3D12_RESOURCE_BARRIER MakeUavBarrier(ID3D12Resource* resource);
D3D12_RESOURCE_BARRIER MakeAliasingBarrier(
    ID3D12Resource* before,
    ID3D12Resource* after);
```

---

## Subresource helper

```cpp
UINT CalcSubresource(
    UINT mipSlice,
    UINT arraySlice,
    UINT planeSlice,
    UINT mipLevels,
    UINT arraySize) noexcept;
```

```text
subresource = mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize
```

---

## FormatUtil

```cpp
bool IsDepthFormat(DXGI_FORMAT format);
bool IsTypelessFormat(DXGI_FORMAT format);
bool IsBlockCompressedFormat(DXGI_FORMAT format);
bool IsYuvFormat(DXGI_FORMAT format);
bool IsPlanarFormat(DXGI_FORMAT format);
bool RequiresEvenSize(DXGI_FORMAT format);
UINT GetKnownPlaneCount(DXGI_FORMAT format);
UINT BitsPerPixel(DXGI_FORMAT format);
UINT BytesPerPixel(DXGI_FORMAT format);
```

---

## Debug / ThrowIfFailed

```cpp
void D3D12Debug::EnableDebugLayer(bool enableGpuValidation);
void D3D12Debug::EnableDred();
void D3D12Debug::SetupInfoQueue(
    ID3D12Device* device,
    bool breakOnError,
    bool breakOnCorruption,
    bool breakOnWarning);
void D3D12Debug::PrintDredInfo(ID3D12Device* device);
```

```cpp
std::string HResultToHexString(HRESULT hr);
void ThrowIfFailed(HRESULT hr);
void ThrowIfFailed(HRESULT hr, const char* message);

#define D3D12CORE_THROW_IF_FAILED(hr)          /* ... */
#define D3D12CORE_THROW_IF_FAILED_MSG(hr, msg) /* ... */
```

関連設計は[`GenericGpuFoundationPhase1.md`](GenericGpuFoundationPhase1.md)、[`GenericGpuFoundationPhase3TypedCommandList.md`](GenericGpuFoundationPhase3TypedCommandList.md)を参照してください。
