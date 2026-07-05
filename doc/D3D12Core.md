# D3D12Core リファレンス（Layer 1）

`include/D3D12Helper/D3D12Core` に含まれる型・関数のリファレンスです。すべて名前空間 `D3D12CoreLib` に属します。

- [D3D12Common](#d3d12common)
- [D3D12CoreConfig](#d3d12coreconfig)
- [D3D12Core](#d3d12core)
- [D3D12DeviceContext](#d3d12devicecontext)
- [D3D12Queue](#d3d12queue)
- [D3D12Fence](#d3d12fence)
- [D3D12CommandContext](#d3d12commandcontext)
- [DxgiAdapterSelector](#dxgiadapterselector)
- [Barrier ヘルパ](#barrier-ヘルパ)
- [Subresource ヘルパ](#subresource-ヘルパ)
- [FormatUtil](#formatutil)
- [Debug ユーティリティ](#debug-ユーティリティ)
- [ThrowIfFailed](#throwiffailed)
- [D3D12SharedResource](#d3d12sharedresource)
- [DxgiUtil](#dxgiutil)

---

## D3D12Common

`D3D12Common.hpp` は Layer 1 の共通 include です。`<windows.h>`、`<d3d12.h>`、`<dxgi1_6.h>`、WRL の `ComPtr` などを取り込みます。

```cpp
namespace D3D12CoreLib {
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
}
```

---

## D3D12CoreConfig

`D3D12Core` 初期化時の設定構造体です。

```cpp
struct D3D12CoreConfig {
    bool enableDebugLayer    = true;
    bool enableGpuValidation = false;
    bool enableInfoQueue     = true;
    bool enableDred          = true;

    bool preferHighPerformanceAdapter = true;
    bool allowWarpAdapter             = false;

    bool createDirectQueue  = true;
    bool createComputeQueue = false;
    bool createCopyQueue    = true;

    bool breakOnError      = false;
    bool breakOnCorruption = false;
    bool breakOnWarning    = false;
};
```

`createDirectQueue` の値に関わらず Direct Queue は必ず 1 つ作成されます。`createComputeQueue` / `createCopyQueue` は専用キューの追加生成を制御します。

---

## D3D12Core

Device / Queue / Fence / CommandContext を束ねるファサードです。コピー禁止で、アプリケーション内では `std::shared_ptr<D3D12Core>` を共有して使う想定です。

```cpp
void Initialize(const D3D12CoreConfig& config = {});
void InitializeWithAdapterLuid(LUID luid, const D3D12CoreConfig& config = {});

static std::shared_ptr<D3D12Core> CreateShared(const D3D12CoreConfig& config = {});
static std::shared_ptr<D3D12Core> CreateSharedWithAdapterLuid(
    LUID luid, const D3D12CoreConfig& config = {});

D3D12DeviceContext&       DeviceContext();
const D3D12DeviceContext& DeviceContext() const;

D3D12Queue& DirectQueue();
D3D12Queue& CopyQueue();
D3D12Queue* ComputeQueue();

ID3D12Device*       GetDevice() const;
ID3D12CommandQueue* GetDirectCommandQueue();
LUID                GetAdapterLuid() const;
bool                IsSameAdapter(LUID other) const;

D3D12CommandContext CreateDirectContext();
D3D12CommandContext CreateCopyContext();
D3D12CommandContext CreateComputeContext();

void WaitIdle();
```

---

## D3D12DeviceContext

DXGI Factory / Adapter / Device と LUID・アダプタ名を保持します。

```cpp
ID3D12Device*  GetDevice()  const;
IDXGIFactory6* GetFactory() const;
IDXGIAdapter1* GetAdapter() const;

LUID         GetAdapterLuid() const;
std::wstring GetAdapterName() const;

bool SupportsResourceSharing() const;
bool SupportsTypedUavLoad(DXGI_FORMAT format) const;
```

`SupportsResourceSharing()` は共有リソースが利用可能かを返します。`SupportsTypedUavLoad(format)` は指定フォーマットで typed UAV load が可能かを返します。

---

## D3D12Queue

`ID3D12CommandQueue` と専用 `D3D12Fence` を管理します。

```cpp
void Initialize(ID3D12Device* device,
                D3D12_COMMAND_LIST_TYPE type,
                D3D12_COMMAND_QUEUE_PRIORITY priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

ID3D12CommandQueue*     Get() const;
D3D12_COMMAND_LIST_TYPE GetType() const;
D3D12Fence&             Fence();

void   ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists);
UINT64 Signal();
void   WaitForFenceValue(UINT64 value);
void   WaitIdle();
void   GpuWait(ID3D12Fence* fence, UINT64 value);
```

---

## D3D12Fence

Fence 値の管理・GPU 完了待ち・キュー間同期を担います。

```cpp
void Initialize(ID3D12Device* device);

UINT64 Signal(ID3D12CommandQueue* queue);
void   Wait(UINT64 fenceValue);
void   WaitIdle(ID3D12CommandQueue* queue);

bool        IsCompleted(UINT64 fenceValue) const;
UINT64      GetCurrentValue() const;
UINT64      GetCompletedValue() const;
ID3D12Fence* Get() const;
```

---

## D3D12CommandContext

Command Allocator + Command List をまとめた、明示的な Reset/Close モデルの記録口です。

```cpp
void Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

ID3D12GraphicsCommandList* GetCommandList() const;
ID3D12CommandAllocator*    GetAllocator() const;
D3D12_COMMAND_LIST_TYPE    GetType() const;
bool                       IsOpen() const;

void Reset();
void Close();

void ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);
void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers);
```

`Reset()` の前に、その Allocator で積んだ GPU 処理が完了している必要があります。

---

## DxgiAdapterSelector

DXGI Factory の生成とアダプタ選択を行う静的ヘルパです。

```cpp
static ComPtr<IDXGIFactory6> CreateFactory(bool enableDebug);

static ComPtr<IDXGIAdapter1> SelectHardwareAdapter(
    IDXGIFactory6* factory, bool preferHighPerformance, bool allowWarp);

static ComPtr<IDXGIAdapter1> SelectAdapterByLuid(IDXGIFactory6* factory, LUID luid);
```

---

## Barrier ヘルパ

`D3D12Barrier.hpp` の自由関数です。

```cpp
D3D12_RESOURCE_BARRIER MakeTransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

D3D12_RESOURCE_BARRIER MakeUavBarrier(ID3D12Resource* resource);

D3D12_RESOURCE_BARRIER MakeAliasingBarrier(
    ID3D12Resource* before, ID3D12Resource* after);
```

---

## Subresource ヘルパ

`D3D12Subresource.hpp` の自由関数です。mip slice / array slice / plane slice から D3D12 の subresource index を計算します。

```cpp
UINT CalcSubresource(
    UINT mipSlice,
    UINT arraySlice,
    UINT planeSlice,
    UINT mipLevels,
    UINT arraySize) noexcept;
```

計算式は D3D12 の標準的な subresource 配列順に合わせています。

```cpp
subresource = mipSlice + arraySlice * mipLevels + planeSlice * mipLevels * arraySize;
```

planar format の plane 単位 barrier / copy、mipmap texture、texture array などで使用します。

---

## FormatUtil

`D3D12CoreLib::FormatUtil` 名前空間の `DXGI_FORMAT` 判定ヘルパです。

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

`IsYuvFormat` は packed / planar を含む video 系 format 判定です。`IsPlanarFormat` は NV12 / P010 など複数 plane を持つ format に対して true を返します。`GetKnownPlaneCount` は既知 format の静的 plane 数を返し、`DXGI_FORMAT_UNKNOWN` では 0 を返します。

---

## Debug ユーティリティ

`D3D12CoreLib::D3D12Debug` 名前空間の補助関数です。

```cpp
void EnableDebugLayer(bool enableGpuValidation);
void EnableDred();
void SetupInfoQueue(ID3D12Device* device,
                    bool breakOnError, bool breakOnCorruption, bool breakOnWarning);
void PrintDredInfo(ID3D12Device* device);

template <typename T>
void SetDebugName(T* object, const wchar_t* name);
```

---

## ThrowIfFailed

`HRESULT` を例外化します。

```cpp
std::string HResultToHexString(HRESULT hr);

void ThrowIfFailed(HRESULT hr);
void ThrowIfFailed(HRESULT hr, const char* message);

#define D3D12CORE_THROW_IF_FAILED(hr)          /* ... */
#define D3D12CORE_THROW_IF_FAILED_MSG(hr, msg) /* ... */
```

---

## D3D12SharedResource

D3D11/D3D12 共有や外部 API 連携のための、共有 Handle 作成 / オープン補助です。

```cpp
static HANDLE CreateSharedHandle(
    ID3D12Device* device, ID3D12Resource* resource, LPCWSTR name = nullptr);

static ComPtr<ID3D12Resource> OpenSharedHandle(
    ID3D12Device* device, HANDLE handle);
```

共有するリソースは `D3D12_HEAP_FLAG_SHARED` 付きで作成されている必要があります。`CreateSharedHandle` の戻り値 `HANDLE` は、呼び出し側が `CloseHandle` で解放します。

---

## DxgiUtil

LUID 関連の小ユーティリティです。

```cpp
inline bool LuidEquals(const LUID& a, const LUID& b);
std::wstring LuidToWString(const LUID& luid);
```
