# D3D12Core リファレンス（Layer 1）

`include/D3D12Core` に含まれる型・関数のリファレンスです。すべて名前空間 `D3D12CoreLib` に属します。

- [D3D12Common](#d3d12common) — 共通 include / ComPtr
- [D3D12CoreConfig](#d3d12coreconfig) — 初期化設定
- [D3D12Core](#d3d12core) — ファサード
- [D3D12DeviceContext](#d3d12devicecontext) — Factory / Adapter / Device / LUID
- [D3D12Queue](#d3d12queue) — Queue + 専用 Fence
- [D3D12Fence](#d3d12fence) — Fence 値管理・完了待ち
- [D3D12CommandContext](#d3d12commandcontext) — Allocator + List
- [DxgiAdapterSelector](#dxgiadapterselector) — アダプタ選択
- [Barrier ヘルパ](#barrier-ヘルパ)
- [FormatUtil](#formatutil)
- [Debug ユーティリティ](#debug-ユーティリティ)
- [ThrowIfFailed](#throwiffailed)
- [D3D12SharedResource](#d3d12sharedresource)
- [DxgiUtil](#dxgiutil)

---

## D3D12Common

`D3D12Common.hpp` は Layer 1 の共通 include です。`<windows.h>`（`WIN32_LEAN_AND_MEAN` / `NOMINMAX` 定義済み）、`<d3d12.h>`、`<dxgi1_6.h>`、WRL の `ComPtr` を取り込み、リンク用の `#pragma comment(lib, ...)` を宣言します。

```cpp
namespace D3D12CoreLib {
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
}
```

ライブラリ全体でスマートポインタとして `ComPtr<T>` を使います。

---

## D3D12CoreConfig

`D3D12Core` 初期化時の設定構造体。すべて既定値を持つので、必要な項目だけ上書きします。

```cpp
struct D3D12CoreConfig {
    // --- Debug ---
    bool enableDebugLayer    = true;   // Device 作成前に Debug 層を有効化
    bool enableGpuValidation = false;  // GPU-Based Validation（重い。enableDebugLayer 前提）
    bool enableInfoQueue     = true;   // InfoQueue フィルタ/break 設定
    bool enableDred          = true;   // DRED（Device Removed Extended Data）

    // --- Adapter ---
    bool preferHighPerformanceAdapter = true;  // 高性能 GPU を優先
    bool allowWarpAdapter             = false; // HW が無ければ WARP を許可

    // --- Queue ---
    bool createDirectQueue  = true;    // 注: Direct Queue は常に 1 つ作られる（後述）
    bool createComputeQueue = false;   // 専用 Compute Queue を作る
    bool createCopyQueue    = true;    // 専用 Copy Queue を作る

    // --- InfoQueue break（enableInfoQueue かつ debug 時のみ有効）---
    bool breakOnError      = false;
    bool breakOnCorruption = false;
    bool breakOnWarning    = false;
};
```

> **注意:** `createDirectQueue` の値に関わらず Direct Queue は必ず 1 つ作成されます（Core の前提として最低 1 つのキューを保証するため）。`createComputeQueue` / `createCopyQueue` は専用キューの追加生成を制御します。

---

## D3D12Core

Device / Queue / Fence / CommandContext を束ねるファサード。コピー禁止。**1 つの Core を全サブシステムで shared_ptr 共有する**のが基本方針です。

### 初期化

```cpp
void Initialize(const D3D12CoreConfig& config = {});
void InitializeWithAdapterLuid(LUID luid, const D3D12CoreConfig& config = {});

static std::shared_ptr<D3D12Core> CreateShared(const D3D12CoreConfig& config = {});
static std::shared_ptr<D3D12Core> CreateSharedWithAdapterLuid(
    LUID luid, const D3D12CoreConfig& config = {});
```

`CreateShared*` は `make_shared` で生成して `Initialize*` を呼んだ `shared_ptr` を返します。`Initialize` を二重に呼ぶと例外（`already initialized`）になります。

`InitializeWithAdapterLuid` は特定アダプタ（LUID 指定）で初期化します。マルチ GPU 構成や、他 API とアダプタを揃えたい場合に使います。

### サブオブジェクトへのアクセス

```cpp
D3D12DeviceContext&       DeviceContext();        // const 版あり
D3D12Queue&               DirectQueue();
D3D12Queue&               CopyQueue();
D3D12Queue*               ComputeQueue();          // 未作成なら nullptr
```

`ComputeQueue()` は `createComputeQueue = true` で初期化したときのみ非 null を返します。

### ショートカット

```cpp
ID3D12Device*       GetDevice() const;
ID3D12CommandQueue* GetDirectCommandQueue();
LUID                GetAdapterLuid() const;
bool                IsSameAdapter(LUID other) const;
```

`IsSameAdapter()` は内部で LUID 比較を行います。

### CommandContext 生成

```cpp
D3D12CommandContext CreateDirectContext();
D3D12CommandContext CreateCopyContext();
D3D12CommandContext CreateComputeContext();
```

各 Context は自前の Command Allocator を 1 つ持ちます（戻り値は move されて呼び出し側が所有）。生成直後の Context は **Close 状態**です。記録を始めるには `Reset()` を呼びます。

### 全体フラッシュ

```cpp
void WaitIdle();
```

Direct / Copy / Compute の全キューを Signal して GPU 完了まで待ちます。アプリ終了前やリソース破棄前の安全弁として使います。

---

## D3D12DeviceContext

DXGI Factory / Adapter / Device と LUID・アダプタ名を保持します。通常はアプリから直接初期化せず、`D3D12Core` 経由で生成されたものを `core.DeviceContext()` で参照します。

```cpp
ID3D12Device*  GetDevice()  const;
IDXGIFactory6* GetFactory() const;
IDXGIAdapter1* GetAdapter() const;

LUID         GetAdapterLuid() const;
std::wstring GetAdapterName() const;

bool SupportsResourceSharing() const;
bool SupportsTypedUavLoad(DXGI_FORMAT format) const;   // 指定フォーマットの typed UAV load 可否
```

- `SupportsResourceSharing()` は他 API（D3D11 / CUDA など）とのリソース共有が可能かを返します。
- `SupportsTypedUavLoad(format)` は、そのフォーマットで **typed UAV load**（シェーダから UAV を型付き読み込み）が可能かを返します。UAV へ書き込むだけ（typed store）なら必須ではありません。

---

## D3D12Queue

`ID3D12CommandQueue` と、それ専用の `D3D12Fence` を 1 つ管理します。`D3D12Fence` が move-only のため本クラスも **move-only**（コピー禁止）。

```cpp
void Initialize(ID3D12Device* device,
                D3D12_COMMAND_LIST_TYPE type,
                D3D12_COMMAND_QUEUE_PRIORITY priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL);

ID3D12CommandQueue*     Get() const;
D3D12_COMMAND_LIST_TYPE GetType() const;
D3D12Fence&             Fence();

void   ExecuteCommandLists(UINT count, ID3D12CommandList* const* lists);
UINT64 Signal();                          // 専用 Fence に Signal し、その値を返す
void   WaitForFenceValue(UINT64 value);   // 指定値の完了を CPU で待つ
void   WaitIdle();                        // Signal してフルフラッシュ待ち
void   GpuWait(ID3D12Fence* fence, UINT64 value);  // 他キューの完了を GPU 側で待つ
```

典型的な実行フロー:

```cpp
ID3D12CommandList* lists[] = { ctx.GetCommandList() };
queue.ExecuteCommandLists(1, lists);
UINT64 fv = queue.Signal();
queue.WaitForFenceValue(fv);   // CPU ブロック待ち
```

キュー間同期（例: Copy で作ったリソースを Direct で使う前に待つ）は `GpuWait()` を使います。

```cpp
// Copy キューが fv まで終わるのを Direct キューが GPU 側で待つ
directQueue.GpuWait(copyQueue.Fence().Get(), fv);
```

---

## D3D12Fence

Fence 値の管理・GPU 完了待ち・キュー間同期を担います。`HANDLE`（待機イベント）を所有するため **move-only**（rule of five）。

```cpp
void   Initialize(ID3D12Device* device);

UINT64 Signal(ID3D12CommandQueue* queue);  // キューを独占するスレッドから呼ぶ
void   Wait(UINT64 fenceValue);            // 任意スレッドから可。CPU ブロック待ち
void   WaitIdle(ID3D12CommandQueue* queue);// Signal して完了まで待つ

bool        IsCompleted(UINT64 fenceValue) const;  // ノンブロッキング
UINT64      GetCurrentValue()   const;  // 次に Signal する値（atomic load）
UINT64      GetCompletedValue() const;
ID3D12Fence* Get() const;
```

### スレッド安全性

- `m_nextValue` は `std::atomic<UINT64>`。**Signal するスレッドと、IsCompleted/GetCurrentValue を読むスレッドが別**でも安全です。
- `Signal()` 自体は 1 スレッドから呼ぶ前提（そのキューを独占するスレッド）。
- `Wait()` / `WaitIdle()` は任意スレッドから呼べます。
- Fence 値は `1` から始まります（`0` は「未 Signal」を表す）。

通常はこのクラスを直接触らず、`D3D12Queue` の `Signal/WaitForFenceValue/WaitIdle` を経由します。

---

## D3D12CommandContext

Command Allocator + Command List をまとめた、明示的な **Reset/Close モデル**の記録口。**move-only**（コピー禁止）。

```cpp
void Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

ID3D12GraphicsCommandList* GetCommandList() const;
ID3D12CommandAllocator*    GetAllocator()   const;
D3D12_COMMAND_LIST_TYPE    GetType()        const;
bool                       IsOpen()         const;

void Reset();   // Allocator + List をリセットして記録開始
void Close();   // 記録終了（List を Close）

void ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);
void ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER* barriers);
```

### ライフサイクル

1. `core.CreateDirectContext()` 等で生成。**生成直後は Close 状態**。
2. `Reset()` で記録開始（`IsOpen()` が true に）。
3. `GetCommandList()` に対してコマンドを積む。
4. `Close()` で記録終了。
5. キューで `ExecuteCommandLists`。
6. 次フレームで再び `Reset()`。

### 重要な制約

- 本クラスは Allocator を **1 つだけ**持ちます。`Reset()` の前に、その Allocator で積んだ GPU 処理が**完了している必要があります**（呼び出し側が Fence で保証する）。フレーム単位の Allocator プール化は将来フェーズ対応です。
- `Reset()` を Open 状態で呼ぶと例外になります（先に `Close()` が必要）。
- `Close()` の二重呼び出しは無害（何もしない）。

---

## DxgiAdapterSelector

DXGI Factory の生成とアダプタ選択を行う静的ヘルパ。通常は `D3D12Core` が内部で使うため、直接呼ぶ必要は少ないです。

```cpp
static ComPtr<IDXGIFactory6> CreateFactory(bool enableDebug);

static ComPtr<IDXGIAdapter1> SelectHardwareAdapter(
    IDXGIFactory6* factory, bool preferHighPerformance, bool allowWarp);

static ComPtr<IDXGIAdapter1> SelectAdapterByLuid(IDXGIFactory6* factory, LUID luid);
```

- `CreateFactory(true)` は `DXGI_CREATE_FACTORY_DEBUG` を試み、Debug 層が無ければ自動で非デバッグ再試行します。
- `SelectHardwareAdapter` は D3D12 Device を作成できる最初の HW アダプタを返します。見つからず `allowWarp = true` なら WARP を返し、両方無ければ `nullptr`。
- `SelectAdapterByLuid` は LUID 一致かつ D3D12 対応のアダプタを返します。見つからない / 非対応なら例外。

---

## Barrier ヘルパ

`D3D12Barrier.hpp` の自由関数。明示的バリアが基本方針です。

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

作った barrier は `ctx.ResourceBarrier(...)` で積みます。

```cpp
ctx.ResourceBarrier(
    MakeTransitionBarrier(tex.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE));
```

`MakeUavBarrier` は同一 UAV への連続書き込み間の同期（read-after-write など）に使います。

---

## FormatUtil

`D3D12CoreLib::FormatUtil` 名前空間の `DXGI_FORMAT` 判定ヘルパ。

```cpp
bool IsDepthFormat(DXGI_FORMAT format);
bool IsTypelessFormat(DXGI_FORMAT format);
bool IsBlockCompressedFormat(DXGI_FORMAT format);

UINT BitsPerPixel(DXGI_FORMAT format);   // 1 px あたりビット数（BC は代表値）
UINT BytesPerPixel(DXGI_FORMAT format);  // バイト数（切り上げ）。BC 形式は 0 を返す
```

`BytesPerPixel` が `0` を返すフォーマット（ブロック圧縮）は、メモリからのアップロードヘルパでは扱えません（ブロック単位で扱うべきため）。

---

## Debug ユーティリティ

`D3D12CoreLib::D3D12Debug` 名前空間。`D3D12Core` が config に応じて内部で呼びますが、個別利用も可能です。

```cpp
void EnableDebugLayer(bool enableGpuValidation);  // Device 作成「前」に呼ぶ
void EnableDred();                                // Device 作成「前」に呼ぶ
void SetupInfoQueue(ID3D12Device* device,
                    bool breakOnError, bool breakOnCorruption, bool breakOnWarning);  // 作成「後」
void PrintDredInfo(ID3D12Device* device);         // Device Removed 後に DRED 情報を出力

template <typename T>
void SetDebugName(T* object, const wchar_t* name); // オブジェクトにデバッグ名を付与
```

- `EnableDebugLayer` は Debug 層が無い環境（製品環境など）では**致命扱いにせず**黙って戻ります。
- `SetDebugName` はデバッガ / PIX 上の表示名を付けるのに便利です。リソース・PSO・キューなど任意の名前付き D3D12 オブジェクトに使えます。

```cpp
D3D12Debug::SetDebugName(core->GetDevice(), L"MainDevice");
```

---

## ThrowIfFailed

`HRESULT` を例外化します。詳細版は呼び出し式・ファイル・行・任意メッセージを例外に含めます。

```cpp
std::string HResultToHexString(HRESULT hr);

void ThrowIfFailed(HRESULT hr);
void ThrowIfFailed(HRESULT hr, const char* message);

// マクロ（呼び出し箇所情報を自動付与）
#define D3D12CORE_THROW_IF_FAILED(hr)          /* ... */
#define D3D12CORE_THROW_IF_FAILED_MSG(hr, msg) /* ... */
```

ライブラリ内部では基本的にマクロ版を使っています。利用側でも同様に使えます。

```cpp
D3D12CORE_THROW_IF_FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
D3D12CORE_THROW_IF_FAILED_MSG(hr, "swapchain creation failed");
```

---

## D3D12SharedResource

D3D11/D3D12 共有や外部 API（CUDA / Varjo など）連携のための、共有 Handle 作成 / オープンの低レイヤ補助。Core 自体は外部 API に依存しません。

```cpp
static HANDLE CreateSharedHandle(
    ID3D12Device* device, ID3D12Resource* resource, LPCWSTR name = nullptr);

static ComPtr<ID3D12Resource> OpenSharedHandle(
    ID3D12Device* device, HANDLE handle);
```

- 共有するリソースは `D3D12_HEAP_FLAG_SHARED` 付きで作成されている必要があります。
- `CreateSharedHandle` の戻り値 `HANDLE` は、呼び出し側が `CloseHandle` で解放してください。

---

## DxgiUtil

LUID 関連の小ユーティリティ。

```cpp
inline bool LuidEquals(const LUID& a, const LUID& b);  // 2 つの LUID が同一か
std::wstring LuidToWString(const LUID& luid);          // "High,Low" 形式の文字列
```
