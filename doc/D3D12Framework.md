# D3D12Framework リファレンス（Layer 2）

`include/D3D12Helper/D3D12Framework` に含まれる型・関数のリファレンスです。すべて名前空間 `D3D12CoreLib` に属します。`D3D12Framework.hpp` をインクルードすると以下がまとめて取り込まれます。

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

`ID3D12Resource` と「現在の Resource State」を持つ move-only ラッパです。

```cpp
class D3D12Resource {
public:
    D3D12Resource() = default;
    D3D12Resource(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES state);

    ID3D12Resource*  Get() const;
    ID3D12Resource** GetAddressOf();
    explicit operator bool() const;

    D3D12_RESOURCE_STATES GetState() const;
    void                  SetState(D3D12_RESOURCE_STATES state);

    D3D12_RESOURCE_DESC GetDesc() const;
    UINT64      GetWidth()  const;
    UINT        GetHeight() const;
    DXGI_FORMAT GetFormat() const;
};
```

`m_state` は **単一状態の簡易追跡**です。mip / array / planar などサブリソース単位で状態が異なる場合、複数 Queue で同時参照する場合、外部 API と共有する場合は、before / after を明示して手動管理してください。

---

## D3D12DescriptorHandle

CPU/GPU ディスクリプタハンドルのペアと付随情報です。

```cpp
struct D3D12DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    UINT index = 0;
    bool shaderVisible = false;

    bool IsValid() const;
};
```

---

## D3D12DescriptorHeap / D3D12DescriptorAllocator

Descriptor Heap 本体と、個別 Free を持たない線形 Allocator です。

```cpp
class D3D12DescriptorHeap {
public:
    void Initialize(ID3D12Device* device,
                    D3D12_DESCRIPTOR_HEAP_TYPE type,
                    UINT count, bool shaderVisible);

    ID3D12DescriptorHeap*      Get() const;
    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const;
    UINT GetDescriptorSize() const;
    UINT GetCapacity() const;
    bool IsShaderVisible() const;

    D3D12DescriptorHandle GetHandle(UINT index) const;
};

class D3D12DescriptorAllocator {
public:
    void Initialize(ID3D12Device* device,
                    D3D12_DESCRIPTOR_HEAP_TYPE type,
                    UINT count, bool shaderVisible);

    D3D12DescriptorHandle Allocate();
    void Reset();

    ID3D12DescriptorHeap* GetHeap() const;
    const D3D12DescriptorHeap& Heap() const;
    UINT GetAllocatedCount() const;
    UINT GetCapacity() const;
};
```

`D3D12DescriptorAllocator` はサブシステムごとに必要なだけ生成して使います。スレッドセーフではありません。

---

## D3D12UploadBuffer

CPU→GPU 転送用の Upload Heap バッファです。persistent map され、move-only です。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

void*  Map() const;
void   Unmap();

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

単発・初期化用の用途が中心です。毎フレームのホットパスでは [D3D12UploadRing](#d3d12uploadring) を推奨します。

---

## D3D12UploadRing

高 fps アップロード向けのリングバッファです。1 つの大きな Upload Heap を確保し、フレームごとに線形に積み進めます。非スレッドセーフ・move-only です。

```cpp
struct Allocation {
    void*           cpuPtr   = nullptr;
    ID3D12Resource* resource = nullptr;
    UINT64          offset   = 0;
    UINT64          size     = 0;
    bool IsValid() const;
};

void Initialize(ID3D12Device* device, UINT64 ringSize);

Allocation Allocate(UINT64 sizeBytes,
                    UINT64 alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
void       ReclaimCompleted(D3D12Fence& fence);
void       FinishFrame(UINT64 fenceValue);

UINT64 GetRingSize()  const;
UINT64 GetUsedBytes() const;
UINT64 GetFreeBytes() const;
```

フレーム冒頭で `ReclaimCompleted`、実行後に `Signal`、その Fence 値を `FinishFrame` に渡すのが基本です。

---

## D3D12ReadbackBuffer

GPU→CPU 戻し用の Readback Heap バッファです。move-only です。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

const void* Map();
void        Unmap();

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

初期状態は `D3D12_RESOURCE_STATE_COPY_DEST` です。GPU 処理完了後に `Map()` で読みます。テクスチャを読み戻す場合は `GetCopyableFootprints` で得た row pitch に従って読んでください。

---

## D3D12ComputePipeline

Compute Shader 用の Root Signature + Pipeline State をまとめます。move-only です。

```cpp
struct ComputePipelineDesc {
    UINT numSrvs               = 0;
    UINT numUavs               = 0;
    UINT numRootConstantValues = 0;
};

class D3D12ComputePipeline {
public:
    void InitializeWithTemplate(ID3D12Device* device,
                                const ShaderBytecode& cs,
                                const ComputePipelineDesc& desc);

    void Initialize(ID3D12Device* device,
                    ComPtr<ID3D12RootSignature> rootSignature,
                    const ShaderBytecode& cs);

    void Bind(D3D12CommandContext& ctx) const;
    void Dispatch(D3D12CommandContext& ctx, UINT groupCountX, UINT groupCountY, UINT groupCountZ) const;

    ID3D12RootSignature* GetRootSignature() const;
    ID3D12PipelineState* GetPipelineState() const;

    UINT SrvTableIndex() const;
    UINT UavTableIndex() const;
    UINT RootConstantsIndex() const;
};
```

`InitializeWithTemplate` では、SRV テーブル（`t0` から `numSrvs` 個）、UAV テーブル（`u0` から `numUavs` 個）、Root Constants（`b0`）を必要なものだけ順に並べた Root Signature を自動生成します。

```cpp
pipeline.Bind(ctx);

ID3D12DescriptorHeap* heaps[] = { alloc.GetHeap() };
ctx.GetCommandList()->SetDescriptorHeaps(1, heaps);
ctx.GetCommandList()->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srv.gpu);
ctx.GetCommandList()->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
ctx.GetCommandList()->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 2, consts, 0);

pipeline.Dispatch(ctx, gx, gy, 1);
```

`Bind` は Root Signature と PSO をセットするだけです。Descriptor Heap、Root Descriptor Table、Root Constants の設定は呼び出し側で行います。

---

## D3D12GraphicsPipeline

グラフィクス用の Root Signature + Pipeline State をまとめます。move-only です。

```cpp
namespace PipelineDefaults {
D3D12_RASTERIZER_DESC Rasterizer(
    D3D12_CULL_MODE cull = D3D12_CULL_MODE_BACK,
    D3D12_FILL_MODE fill = D3D12_FILL_MODE_SOLID,
    bool frontCounterClockwise = false);

D3D12_BLEND_DESC BlendOpaque();
D3D12_BLEND_DESC BlendAlpha();
D3D12_DEPTH_STENCIL_DESC DepthDisabled();
D3D12_DEPTH_STENCIL_DESC DepthDefault(
    bool depthWrite = true,
    D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS);
}

struct GraphicsPipelineDesc {
    ShaderBytecode vs;
    ShaderBytecode ps;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    UINT        numRenderTargets = 1;
    DXGI_FORMAT rtvFormats[8]    = { DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT dsvFormat        = DXGI_FORMAT_UNKNOWN;

    UINT sampleCount   = 1;
    UINT sampleQuality = 0;

    const D3D12_RASTERIZER_DESC*    rasterizer   = nullptr;
    const D3D12_BLEND_DESC*         blend        = nullptr;
    const D3D12_DEPTH_STENCIL_DESC* depthStencil = nullptr;
};

class D3D12GraphicsPipeline {
public:
    void Initialize(ID3D12Device* device,
                    ComPtr<ID3D12RootSignature> rootSignature,
                    const GraphicsPipelineDesc& desc);

    void InitializeRaw(ID3D12Device* device,
                       ComPtr<ID3D12RootSignature> rootSignature,
                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc);

    void Bind(D3D12CommandContext& ctx) const;

    ID3D12RootSignature* GetRootSignature() const;
    ID3D12PipelineState* GetPipelineState() const;
};
```

`Bind` は Graphics Root Signature と PSO をセットするだけです。Viewport、Scissor、RTV/DSV、PrimitiveTopology、VertexBuffer、Root Parameter などは呼び出し側で設定します。

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

ShaderBytecode LoadShaderBytecodeFromFile(const std::filesystem::path& csoPath);

ShaderBytecode CompileShaderFromSource_D3DCompile(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader");

ShaderBytecode CompileShaderFromSource_Dxc(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader",
    const std::vector<std::wstring>& extraArgs = {});
```

---

## リソース生成ヘルパ

`D3D12Helpers.hpp`。すべて第 1 引数に `D3D12Core&` を取り、戻り値（リソース）の所有権は呼び出し側に渡ります。

```cpp
D3D12Resource CreateBuffer(
    D3D12Core& core,
    UINT64 sizeBytes,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateTexture2D(
    D3D12Core& core,
    UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
    UINT16 arraySize = 1, UINT16 mipLevels = 1,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateStructuredBuffer(
    D3D12Core& core,
    UINT elementCount,
    UINT elementStride,
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateConstantBuffer(
    D3D12Core& core,
    UINT64 sizeBytes,
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_GENERIC_READ,
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

D3D12Resource CreateSharedTexture2D(
    D3D12Core& core,
    UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
    UINT16 arraySize = 1, UINT16 mipLevels = 1);
```

`CreateConstantBuffer` は D3D12 の CBV 要件に合わせてサイズを 256 byte アラインします。`CreateSharedTexture2D` は内部で `D3D12_HEAP_FLAG_SHARED` を使い、`D3D12SharedResource::CreateSharedHandle` に渡せる Texture2D を作ります。

---

## テクスチャアップロードヘルパ

### 同期（初期化・単発用）

```cpp
D3D12Resource CreateTexture2DFromMemory(
    D3D12Core& core,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

D3D12Resource CreateTexture2DFromRGBA(
    D3D12Core& core,
    const uint8_t* rgba, UINT width, UINT height,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

D3D12Resource CreateTexture2DFromRGB(
    D3D12Core& core,
    const uint8_t* rgb, UINT width, UINT height,
    uint8_t alpha = 255,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

std::vector<uint8_t> ExpandRGBtoRGBA(
    const uint8_t* rgb, UINT width, UINT height, uint8_t alpha = 255);
```

### ホットパス（コマンドを積むだけ・Wait しない）

```cpp
void RecordUploadTexture2D(
    D3D12Core& core, D3D12CommandContext& ctx,
    D3D12Resource& dstTexture, D3D12UploadBuffer& upload,
    const void* data, UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void RecordUploadTexture2D(
    D3D12Core& core, D3D12CommandContext& ctx,
    D3D12Resource& dstTexture, D3D12UploadRing& ring,
    const void* data, UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture);
```

---

## ディスクリプタ作成ヘルパ

指定の CPU ハンドルに View を作ります。

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

void CreateBufferSrv(
    D3D12Core& core, const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

void CreateBufferUav(
    D3D12Core& core, const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT firstElement,
    UINT numElements,
    UINT structureByteStride = 0,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    ID3D12Resource* counterResource = nullptr);

void CreateConstantBufferView(
    D3D12Core& core, const D3D12Resource& buffer,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    UINT64 byteOffset = 0,
    UINT sizeBytes = 0);

void CreateTexture2DRtv(
    D3D12Core& core, const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);

void CreateTexture2DDsv(
    D3D12Core& core, const D3D12Resource& texture,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN,
    UINT mipSlice = 0);
```

`CreateBufferSrv` / `CreateBufferUav` は Structured Buffer 向けです。`structureByteStride = 0` の場合は buffer size / numElements から推定します。`CreateConstantBufferView` は offset と size を 256 byte アラインで検証します。

---

## SwapChain ヘルパ

```cpp
ComPtr<IDXGISwapChain3> CreateSwapChainForHwnd(
    D3D12Core& core, HWND hwnd,
    UINT width, UINT height,
    UINT bufferCount = 2,
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

D3D12Resource GetSwapChainBackBuffer(IDXGISwapChain3* swapChain, UINT index);
```

Window クラス本体・Present ループ・RTV 運用はアプリ / サンプル側に置く方針です。
