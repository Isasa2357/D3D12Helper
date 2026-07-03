# D3D12Framework リファレンス（Layer 2）

`include/D3D12Framework` に含まれる型・関数のリファレンスです。すべて名前空間 `D3D12CoreLib` に属します。`D3D12Framework.hpp` をインクルードすると以下がまとめて取り込まれます。

ステートフルな building blocks:

- [D3D12Resource](#d3d12resource) — リソース + 状態追跡
- [D3D12DescriptorHandle](#d3d12descriptorhandle) — CPU/GPU ハンドルペア
- [D3D12DescriptorHeap](#d3d12descriptorheap) — Descriptor Heap
- [D3D12DescriptorAllocator](#d3d12descriptorallocator) — 線形 Allocator
- [D3D12UploadBuffer](#d3d12uploadbuffer) — Upload Heap（persistent map）
- [D3D12UploadRing](#d3d12uploadring) — 高 fps 用リングバッファ
- [D3D12ReadbackBuffer](#d3d12readbackbuffer) — Readback Heap
- [D3D12ComputePipeline](#d3d12computepipeline) — Compute RootSig + PSO
- [D3D12GraphicsPipeline](#d3d12graphicspipeline) — Graphics PSO（既定値 + カスタマイズ）
- [ShaderCompiler](#shadercompiler) — シェーダ取得

`D3D12Core&` を取る自由関数群:

- [リソース生成ヘルパ](#リソース生成ヘルパ)
- [テクスチャアップロードヘルパ](#テクスチャアップロードヘルパ)
- [ディスクリプタ作成ヘルパ](#ディスクリプタ作成ヘルパ)
- [SwapChain ヘルパ](#swapchain-ヘルパ)

---

## D3D12Resource

`ID3D12Resource` と「現在の Resource State」を持つラッパ。

```cpp
class D3D12Resource {
public:
    D3D12Resource() = default;
    D3D12Resource(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES state);

    ID3D12Resource*  Get() const;
    ID3D12Resource** GetAddressOf();
    explicit operator bool() const;          // リソースを持っているか

    D3D12_RESOURCE_STATES GetState() const;
    void                  SetState(D3D12_RESOURCE_STATES state);

    D3D12_RESOURCE_DESC GetDesc() const;
    UINT64      GetWidth()  const;
    UINT        GetHeight() const;
    DXGI_FORMAT GetFormat() const;
};
```

### 状態追跡の制約（重要）

`m_state` は **単一状態の簡易追跡**です。次のケースでは正しく機能しないため、`MakeTransitionBarrier` に before/after を明示して**手動管理**してください。

- サブリソースごとに状態が異なる Texture（mip / array / planar）
- 複数 Queue / 複数 CommandList から同時に参照するリソース
- D3D11 / CUDA など外部 API と共有するリソース

バリアを積んでリソースを遷移させたら、`SetState()` で追跡値も更新するのが基本です（ライブラリ内のヘルパは finalState への遷移後に自動で `SetState` します）。

---

## D3D12DescriptorHandle

CPU/GPU ディスクリプタハンドルのペアと付随情報。

```cpp
struct D3D12DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};   // shaderVisible のときのみ有効
    UINT index = 0;
    bool shaderVisible = false;

    bool IsValid() const;   // cpu.ptr != 0
};
```

---

## D3D12DescriptorHeap

Descriptor Heap 本体。

```cpp
void Initialize(ID3D12Device* device,
                D3D12_DESCRIPTOR_HEAP_TYPE type,
                UINT count, bool shaderVisible);

ID3D12DescriptorHeap*      Get() const;
D3D12_DESCRIPTOR_HEAP_TYPE GetType() const;
UINT GetDescriptorSize() const;
UINT GetCapacity() const;
bool IsShaderVisible() const;

D3D12DescriptorHandle GetHandle(UINT index) const;   // index 番目のハンドル
```

`GetHandle(index)` の GPU ハンドルは `shaderVisible = true` で作成したときのみ有効です。シェーダから参照するヒープ（CBV_SRV_UAV / SAMPLER）は shaderVisible、RTV / DSV は非 shaderVisible で作るのが通常です。

---

## D3D12DescriptorAllocator

単純な**線形 Allocator**。個別 Free はせず、必要なら `Reset()` でまとめて巻き戻します。内部に `D3D12DescriptorHeap` を 1 つ持ちます。

```cpp
void Initialize(ID3D12Device* device,
                D3D12_DESCRIPTOR_HEAP_TYPE type,
                UINT count, bool shaderVisible);

D3D12DescriptorHandle Allocate();   // 次の 1 つを確保。容量超過で例外
void                  Reset();      // 確保位置を 0 に戻す（既存ハンドルは無効化）

ID3D12DescriptorHeap*      GetHeap() const;
const D3D12DescriptorHeap& Heap() const;
UINT GetAllocatedCount() const;
UINT GetCapacity() const;
```

### 所有・スレッド方針

- Core が単一所有するのではなく、**サブシステムごとに必要なだけ生成**して使います。
- `Reset()` のタイミングは各サブシステムが自分で管理します。`Reset()` 後は以前に配ったハンドルが無効になる点に注意。
- **スレッドセーフではありません**。並行アクセスする場合は呼び出し側で排他してください。

```cpp
D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                 /*count*/ 16, /*shaderVisible*/ true);

D3D12DescriptorHandle srv = alloc.Allocate();   // index 0
D3D12DescriptorHandle uav = alloc.Allocate();   // index 1
```

---

## D3D12UploadBuffer

CPU→GPU 転送用の Upload Heap バッファ。**persistent map**（生成中ずっと Map したまま）。**move-only**。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

void*  Map() const;     // persistent map された CPU ポインタ
void   Unmap();         // 明示解放したい場合のみ（通常はデストラクタ任せ）

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

`Map()` は確保済みの CPU ポインタを返すだけ（毎回 Map/Unmap しない）。書き込んだ内容を `CopyBufferRegion` / `CopyTextureRegion` で GPU リソースへコピーします。単発・初期化用の用途が中心で、毎フレームのホットパスでは [D3D12UploadRing](#d3d12uploadring) を推奨します。

---

## D3D12UploadRing

高 fps アップロード向けのリングバッファ。1 つの大きな Upload Heap を確保し、フレームごとに線形に積み進めます。**非スレッドセーフ・move-only**。

```cpp
struct Allocation {
    void*           cpuPtr   = nullptr;  // 書き込み先（Ring の persistent map 内）
    ID3D12Resource* resource = nullptr;  // Ring 全体の ID3D12Resource
    UINT64          offset   = 0;        // Ring 先頭からのバイトオフセット
    UINT64          size     = 0;
    bool IsValid() const;
};

void Initialize(ID3D12Device* device, UINT64 ringSize);

Allocation Allocate(UINT64 sizeBytes,
                    UINT64 alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
void       ReclaimCompleted(D3D12Fence& fence);  // GPU 完了済み領域を回収（ノンブロッキング）
void       FinishFrame(UINT64 fenceValue);       // 現フレームの確保を Fence 値で紐付け

UINT64 GetRingSize()  const;
UINT64 GetUsedBytes() const;
UINT64 GetFreeBytes() const;
```

### フレームの回し方

```cpp
ring.ReclaimCompleted(queue.Fence());                  // フレーム冒頭で空き確保
RecordUploadTexture2D(core, ctx, dst, ring, data, ...);// コマンド記録（Wait しない）
queue.ExecuteCommandLists(1, lists);
UINT64 fv = queue.Signal();
ring.FinishFrame(fv);                                  // この位置を Fence 値で記録
```

### 注意

- `Allocate` は空き不足で**例外**を投げます。先に `ReclaimCompleted` を呼んでください。
- リングサイズは「最大フレームサイズ × インフライト数」以上を確保してください。
- 1 スレッド 1 リングの想定です。

---

## D3D12ReadbackBuffer

GPU→CPU 戻し用の Readback Heap バッファ。**move-only**。Map/Unmap は読み出し時に行います。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

const void* Map();      // GPU 処理完了「後」に呼ぶ。読み出し可能な CPU ポインタ
void        Unmap();

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

初期状態は `D3D12_RESOURCE_STATE_COPY_DEST`。`CopyTextureRegion` / `CopyBufferRegion` でコピー → キューを Signal → **完了を待ってから** `Map()` で読みます。テクスチャを読み戻す場合は `GetCopyableFootprints` で得た行ピッチ（256 アライン）に従って読んでください。

```cpp
const auto* p = static_cast<const uint8_t*>(readback.Map());
// p を rowPitch 単位で読む
readback.Unmap();
```

---

## D3D12ComputePipeline

Compute Shader 用の Root Signature + Pipeline State をまとめます。**move-only**。

### テンプレート Root Signature

多くの compute shader に十分なテンプレを自動生成できます。形は次の通り。

- Root Parameter: SRV テーブル（`t0` から `numSrvs` 個）
- Root Parameter: UAV テーブル（`u0` から `numUavs` 個）
- Root Parameter: Root Constants（`b0`、`numRootConstantValues` 個の DWORD）

`numXxx` を 0 にすると、その slot は省略されます。SRV→UAV→RootConstants の順に、存在するものだけが Root Parameter として並びます。

```cpp
struct ComputePipelineDesc {
    UINT numSrvs               = 0;
    UINT numUavs               = 0;
    UINT numRootConstantValues = 0;   // b0 の DWORD 数。0 なら b0 を作らない
};

void InitializeWithTemplate(ID3D12Device* device,
                            const ShaderBytecode& cs,
                            const ComputePipelineDesc& desc);

// 自前の Root Signature と bytecode で初期化
void Initialize(ID3D12Device* device,
                ComPtr<ID3D12RootSignature> rootSignature,
                const ShaderBytecode& cs);

ID3D12RootSignature* GetRootSignature() const;
ID3D12PipelineState* GetPipelineState() const;

// テンプレ生成時の Root Parameter Index（未使用 slot は UINT_MAX）
UINT SrvTableIndex()      const;
UINT UavTableIndex()      const;
UINT RootConstantsIndex() const;
```

`InitializeWithTemplate` で作った場合、各 slot のルートパラメータ番号は `SrvTableIndex()` 等で取得します（省略した slot は `UINT_MAX`）。Dispatch 時のバインドにこの番号を使います。

```cpp
auto* cl = ctx.GetCommandList();
cl->SetComputeRootSignature(pipeline.GetRootSignature());
cl->SetPipelineState(pipeline.GetPipelineState());
cl->SetDescriptorHeaps(1, heaps);

cl->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srv.gpu);
cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 2, consts, 0);
cl->Dispatch(gx, gy, 1);
```

より複雑な Root Signature（複数 space / static sampler / CBV ルートデスクリプタ等）が必要なら、自前で作って `Initialize(device, rootSig, cs)` を使います。

---

## D3D12GraphicsPipeline

グラフィクス用の Root Signature + Pipeline State をまとめます。**move-only**。`D3D12ComputePipeline` と対称に、用途に応じた **3 段のカスタマイズ経路**を持ちます。グラフィクス PSO は設定項目が多いため、ライブラリは「良い既定値での簡単生成」と「フル制御のエスケープ」の両方を提供します。

### PipelineDefaults（既定 state のプリセット）

`D3D12CoreLib::PipelineDefaults` 名前空間。`D3D12_GRAPHICS_PIPELINE_STATE_DESC` の各 state を手で埋めずに済むよう、よく使う既定値を返します。返り値は値型なので、受け取って一部だけ書き換えても使えます。

```cpp
D3D12_RASTERIZER_DESC    Rasterizer(D3D12_CULL_MODE cull = D3D12_CULL_MODE_BACK,
                                    D3D12_FILL_MODE fill = D3D12_FILL_MODE_SOLID,
                                    bool frontCounterClockwise = false);
D3D12_BLEND_DESC         BlendOpaque();   // ブレンド無し・全成分書き込み
D3D12_BLEND_DESC         BlendAlpha();    // 標準アルファ over 合成
D3D12_DEPTH_STENCIL_DESC DepthDisabled(); // 深度・ステンシル無効
D3D12_DEPTH_STENCIL_DESC DepthDefault(bool depthWrite = true,
                                      D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS);
```

### GraphicsPipelineDesc

簡単経路・上書き経路で使う記述子。`rasterizer` / `blend` / `depthStencil` が `nullptr` のときは `PipelineDefaults` を使います（`depthStencil` 未指定時は `dsvFormat` の有無で `DepthDefault` / `DepthDisabled` を自動選択）。

```cpp
struct GraphicsPipelineDesc {
    ShaderBytecode vs;
    ShaderBytecode ps;          // 空なら「PS 無し」（深度のみ等）

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;   // 空可（SV_VertexID 生成等）

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    UINT        numRenderTargets = 1;
    DXGI_FORMAT rtvFormats[8]    = { DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT dsvFormat        = DXGI_FORMAT_UNKNOWN;   // UNKNOWN なら深度添付無し

    UINT sampleCount   = 1;     // MSAA
    UINT sampleQuality = 0;

    const D3D12_RASTERIZER_DESC*    rasterizer   = nullptr;  // nullptr なら既定値
    const D3D12_BLEND_DESC*         blend        = nullptr;
    const D3D12_DEPTH_STENCIL_DESC* depthStencil = nullptr;
};
```

> **寿命の注意:** `inputLayout` と、`rasterizer` / `blend` / `depthStencil` が指す desc は `Initialize` を呼ぶ瞬間まで生きている必要があります（PSO に焼かれるのは `Initialize` の中です）。`GraphicsPipelineDesc` とポインタ先のローカルを、`Initialize` 完了まで破棄しないでください。

### メソッド

```cpp
// 1) かんたん経路 / 上書き経路。desc から PSO を組む（未指定 state は既定値）。
void Initialize(ID3D12Device* device,
                ComPtr<ID3D12RootSignature> rootSignature,
                const GraphicsPipelineDesc& desc);

// 2) フル制御経路。完成済みの PSO desc をそのまま渡す。
//    pRootSignature は内部で rootSignature 引数に差し替える。
void InitializeRaw(ID3D12Device* device,
                   ComPtr<ID3D12RootSignature> rootSignature,
                   const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc);

ID3D12RootSignature* GetRootSignature() const;
ID3D12PipelineState* GetPipelineState() const;
```

### 使用例（3 段階）

```cpp
// --- Tier 1: かんたん（state は全部既定）---
GraphicsPipelineDesc gd;
gd.vs = std::move(vs);
gd.ps = std::move(ps);
gd.inputLayout = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
gd.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

D3D12GraphicsPipeline pipeline;
pipeline.Initialize(device, rootSig, gd);

// --- Tier 2: 既定 state を一部だけ上書き ---
D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
D3D12_BLEND_DESC      alpha  = PipelineDefaults::BlendAlpha();
gd.rasterizer = &noCull;     // カリング無効
gd.blend      = &alpha;      // アルファブレンド
gd.dsvFormat  = DXGI_FORMAT_D32_FLOAT;   // 深度添付 → 既定で深度テスト有効
pipeline.Initialize(device, rootSig, gd);

// --- Tier 3: フル制御（自分で D3D12_GRAPHICS_PIPELINE_STATE_DESC を全部埋める）---
D3D12_GRAPHICS_PIPELINE_STATE_DESC full = {};
// ... 全フィールドを埋める ...
pipeline.InitializeRaw(device, rootSig, full);
```

描画時:

```cpp
cl->SetGraphicsRootSignature(pipeline.GetRootSignature());
cl->SetPipelineState(pipeline.GetPipelineState());
cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
// ... 頂点バッファ等をバインドして Draw ...
```

> Root Signature 自体はこのクラスでは生成しません（Graphics は CBV/SRV を含む形が多様なため）。空 Root Signature や、変換行列・テクスチャを持つ Root Signature はアプリ側で作って渡します。完全な例は [`../sample/03_HelloTriangle`](../sample/03_HelloTriangle) を参照。

---

## ShaderCompiler

シェーダバイトコードを取得する 3 つの方法を提供します。戻り値の `ShaderBytecode` はバイト列を所有し、`AsD3D12()` で `D3D12_SHADER_BYTECODE` を得られます。

```cpp
class ShaderBytecode {
public:
    explicit ShaderBytecode(std::vector<uint8_t> data);
    const void* Data() const;
    size_t      Size() const;
    bool        Empty() const;
    D3D12_SHADER_BYTECODE AsD3D12() const;
};

// 1. 事前コンパイル済み .cso を読む
ShaderBytecode LoadShaderBytecodeFromFile(const std::filesystem::path& csoPath);

// 2. D3DCompile（d3dcompiler）でランタイムコンパイル（SM 5.1 まで）
//    target 例: "cs_5_1", "vs_5_1", "ps_5_1"
ShaderBytecode CompileShaderFromSource_D3DCompile(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader");

// 3. DXC でランタイムコンパイル（SM 6.0+ / DXIL）
//    target 例: "cs_6_0", "cs_6_6"
ShaderBytecode CompileShaderFromSource_Dxc(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader",
    const std::vector<std::wstring>& extraArgs = {});
```

| 方法 | シェーダモデル | 必要 DLL | 備考 |
| --- | --- | --- | --- |
| `LoadShaderBytecodeFromFile` | 任意（事前ビルド） | なし | ビルド時にコンパイルした `.cso` を読むだけ。最速・確実 |
| `CompileShaderFromSource_D3DCompile` | ~ SM 5.1 | `d3dcompiler_47.dll`（OS 同梱） | 手軽。SM 6.0 以降は不可 |
| `CompileShaderFromSource_Dxc` | SM 6.0+ | `dxcompiler.dll`（+ `dxil.dll`） | DXIL 生成。DLL が無いと例外 |

- DXC 版は `_DEBUG` ビルドで `-Zi -Od`、それ以外で `-O3` を自動付与します。追加引数は `extraArgs` で渡せます。
- コンパイルエラーは、DXC のメッセージを含む `std::runtime_error` として投げられます。

---

## リソース生成ヘルパ

`D3D12Helpers.hpp`。すべて第 1 引数に `D3D12Core&` を取り、戻り値（リソース）の所有権は呼び出し側に渡ります。

```cpp
D3D12Resource CreateBuffer(
    D3D12Core& core,
    UINT64 sizeBytes,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

D3D12Resource CreateTexture2D(
    D3D12Core& core,
    UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    UINT16 arraySize = 1, UINT16 mipLevels = 1);
```

いずれも待ち無しで空のリソースを `CreateCommittedResource` で作ります。UAV として使うテクスチャは `flags` に `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` を付けてください（[CreateTexture2DUav](#ディスクリプタ作成ヘルパ) が要求します）。

---

## テクスチャアップロードヘルパ

### 同期（初期化・単発用）

内部で Upload Buffer 確保 → Copy → **GPU 完了待ち** まで行います。待つので毎フレームのホットパスには使わないでください。戻り値テクスチャの状態は `finalState` になります。

```cpp
D3D12Resource CreateTexture2DFromMemory(
    D3D12Core& core,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,   // 0 なら width * BytesPerPixel(format)
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

D3D12Resource CreateTexture2DFromRGBA(   // 4 byte/px
    D3D12Core& core,
    const uint8_t* rgba, UINT width, UINT height,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

D3D12Resource CreateTexture2DFromRGB(    // 3 byte/px → 内部で RGBA8 展開
    D3D12Core& core,
    const uint8_t* rgb, UINT width, UINT height,
    uint8_t alpha = 255,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// RGB → RGBA8 展開だけ行うユーティリティ
std::vector<uint8_t> ExpandRGBtoRGBA(
    const uint8_t* rgb, UINT width, UINT height, uint8_t alpha = 255);
```

- DXGI に 24bit RGB フォーマットが無いため、`CreateTexture2DFromRGB` は alpha を補って `R8G8B8A8_UNORM` にします。
- ブロック圧縮フォーマットは `BytesPerPixel` が 0 を返すため、これらの from-memory ヘルパでは扱えません（例外になります）。

### ホットパス（コマンドを積むだけ・Wait しない）

```cpp
// Upload Buffer 版（upload は呼び出し側が用意し、GPU 完了まで生かす）
void RecordUploadTexture2D(
    D3D12Core& core, D3D12CommandContext& ctx,
    D3D12Resource& dstTexture, D3D12UploadBuffer& upload,
    const void* data, UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// Upload Ring 版（Ring から自動確保）
void RecordUploadTexture2D(
    D3D12Core& core, D3D12CommandContext& ctx,
    D3D12Resource& dstTexture, D3D12UploadRing& ring,
    const void* data, UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// dstTexture アップロードに必要な Upload Buffer の最小サイズ
UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture);
```

- どちらも `ctx` は呼び出し側で `Reset()` 済みであること。実行・Signal・Fence 待ちは呼び出し側責任です。
- コピー後 `dstTexture` を `finalState` へ遷移し、`dstTexture.SetState(finalState)` まで行います。
- Upload Buffer 版は upload を GPU 完了まで生かす必要があります（Ring 版を推奨）。
- Ring 版は `Execute → Signal → ring.FinishFrame(fv)`、次フレーム冒頭で `ring.ReclaimCompleted(fence)` を呼んでください。

---

## ディスクリプタ作成ヘルパ

指定の CPU ハンドルに View を作ります。`format` に `DXGI_FORMAT_UNKNOWN` を渡すとテクスチャのフォーマットを使います。

```cpp
void CreateTexture2DSrv(
    D3D12Core& core, const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

void CreateTexture2DUav(
    D3D12Core& core, const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);
```

- `CreateTexture2DUav` は、対象テクスチャが `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` 付きで作られていないと例外を投げます。
- ハンドルは `D3D12DescriptorAllocator::Allocate()` で得た `D3D12DescriptorHandle::cpu` を渡します。

```cpp
auto srv = alloc.Allocate();
CreateTexture2DSrv(*core, inputTex, srv.cpu);

auto uav = alloc.Allocate();
CreateTexture2DUav(*core, outputTex, uav.cpu);
```

---

## SwapChain ヘルパ

`D3D12SwapChainHelper.hpp`。**作成ヘルパだけ**を提供します。Window クラス本体・Present ループ・RTV 運用はアプリ / サンプル側に置く方針です。

```cpp
ComPtr<IDXGISwapChain3> CreateSwapChainForHwnd(
    D3D12Core& core, HWND hwnd,
    UINT width, UINT height,
    UINT bufferCount = 2,
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

D3D12Resource GetSwapChainBackBuffer(IDXGISwapChain3* swapChain, UINT index);
```

- `CreateSwapChainForHwnd` は `core.DirectQueue()` 上に `FLIP_DISCARD` のスワップチェーンを作り、`IDXGISwapChain3` を返します（`GetCurrentBackBufferIndex` が使えます）。
- `GetSwapChainBackBuffer` は index 番目のバックバッファを `D3D12Resource` として返します（状態は `PRESENT` 扱い）。RTV はアプリ側で別途作成してください。
