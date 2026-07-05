# D3D12Framework リファレンス（Layer 2）

`include/D3D12Helper/D3D12Framework` に含まれる型・関数のリファレンスです。すべて名前空間 `D3D12CoreLib` に属します。`D3D12Framework.hpp` をインクルードすると、Layer 2 の主要ヘッダがまとめて取り込まれます。

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
    void SetState(D3D12_RESOURCE_STATES state);

    D3D12_RESOURCE_DESC GetDesc() const;
    UINT64      GetWidth()  const;
    UINT        GetHeight() const;
    DXGI_FORMAT GetFormat() const;
};
```

`m_state` は単一状態の簡易追跡です。mip / array / planar などサブリソース単位で状態が異なる場合、複数 Queue で同時参照する場合、外部 API と共有する場合は、before / after を明示して手動管理してください。

---

## D3D12DescriptorHandle / D3D12DescriptorRange

CPU/GPU descriptor handle と付随情報です。

```cpp
struct D3D12DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    UINT index = 0;
    bool shaderVisible = false;

    bool IsValid() const;
};

struct D3D12DescriptorRange {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
    UINT startIndex = 0;
    UINT count = 0;
    UINT descriptorSize = 0;
    bool shaderVisible = false;

    bool IsValid() const;
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const;
};
```

`Gpu(index)` は shader-visible heap の range でのみ有効です。

---

## D3D12DescriptorHeap / D3D12DescriptorAllocator

Descriptor Heap 本体と、個別 Free を持たない線形 allocator です。

```cpp
class D3D12DescriptorHeap {
public:
    void Initialize(ID3D12Device* device,
                    D3D12_DESCRIPTOR_HEAP_TYPE type,
                    UINT count, bool shaderVisible);

    ID3D12DescriptorHeap* Get() const;
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
    D3D12DescriptorRange  AllocateRange(UINT count);
    void Reset();

    ID3D12DescriptorHeap* GetHeap() const;
    const D3D12DescriptorHeap& Heap() const;
    UINT GetAllocatedCount() const;
    UINT GetCapacity() const;
};
```

`AllocateRange` は descriptor table を構築するために、連続した descriptor 範囲を確保します。allocator は非スレッドセーフです。

---

## D3D12UploadBuffer

CPU→GPU 転送用の Upload Heap バッファです。persistent map され、move-only です。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

void* Map() const;
void  Unmap();

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

単発・初期化用の用途が中心です。毎フレームのホットパスでは `D3D12UploadRing` を推奨します。

---

## D3D12UploadRing

高 fps アップロード向けのリングバッファです。

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
void ReclaimCompleted(D3D12Fence& fence);
void FinishFrame(UINT64 fenceValue);

UINT64 GetRingSize() const;
UINT64 GetUsedBytes() const;
UINT64 GetFreeBytes() const;
```

非スレッドセーフです。1 スレッド 1 ring の想定です。

---

## D3D12ReadbackBuffer

GPU→CPU 戻し用の Readback Heap バッファです。

```cpp
void Initialize(ID3D12Device* device, UINT64 sizeBytes);

const void* Map();
void        Unmap();

ID3D12Resource* Get() const;
UINT64 GetSizeBytes() const;
```

初期状態は `D3D12_RESOURCE_STATE_COPY_DEST` です。

---

## D3D12ComputePipeline

Compute Shader 用の Root Signature + Pipeline State をまとめます。

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
    void Bind(ID3D12GraphicsCommandList* cmd) const;

    void Dispatch(D3D12CommandContext& ctx, UINT groupCountX, UINT groupCountY, UINT groupCountZ) const;
    void Dispatch(ID3D12GraphicsCommandList* cmd, UINT groupCountX, UINT groupCountY, UINT groupCountZ) const;

    ID3D12RootSignature* GetRootSignature() const;
    ID3D12PipelineState* GetPipelineState() const;

    UINT SrvTableIndex() const;
    UINT UavTableIndex() const;
    UINT RootConstantsIndex() const;
};
```

`Bind` は Root Signature と PSO をセットするだけです。Descriptor Heap、Root Descriptor Table、Root Constants の設定は呼び出し側で行います。

---

## D3D12GraphicsPipeline

Graphics Root Signature + Pipeline State をまとめます。

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
    void Bind(ID3D12GraphicsCommandList* cmd) const;

    ID3D12RootSignature* GetRootSignature() const;
    ID3D12PipelineState* GetPipelineState() const;
};
```

`Bind` は Graphics Root Signature と PSO をセットするだけです。Viewport、Scissor、RTV/DSV、PrimitiveTopology、VertexBuffer、Root Parameter などは呼び出し側で設定します。

---

## ShaderCompiler

シェーダバイトコードを取得する API です。戻り値の `ShaderBytecode` は bytecode を所有し、`AsD3D12()` で `D3D12_SHADER_BYTECODE` を得られます。

```cpp
class ShaderBytecode {
public:
    explicit ShaderBytecode(std::vector<uint8_t> data);
    const void* Data() const;
    size_t      Size() const;
    bool        Empty() const;
    D3D12_SHADER_BYTECODE AsD3D12() const;
};

struct ShaderMacro {
    std::string name;
    std::string value;
};

struct ShaderCompileDesc {
    std::filesystem::path sourcePath;
    std::string entryPoint;
    std::string target;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<ShaderMacro> defines;
    bool useDxc = false;
};

ShaderBytecode LoadShaderBytecodeFromFile(const std::filesystem::path& csoPath);

ShaderBytecode CompileShaderFromFile(const ShaderCompileDesc& desc);
ShaderBytecode CompileShaderFromSource(
    const std::string& hlslSource,
    const ShaderCompileDesc& desc,
    const std::string& sourceName = "shader");

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

`CompileShaderFromFile` / `CompileShaderFromSource` は `includeDirs` と `defines` に対応します。`useDxc = false` では D3DCompile、`true` では DXC を使います。

---

## リソース生成ヘルパ

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

`CreateConstantBuffer` は D3D12 の CBV 要件に合わせてサイズを 256 byte アラインします。`CreateSharedTexture2D` は `D3D12_HEAP_FLAG_SHARED` を使って共有可能 Texture2D を作成します。

---

## テクスチャアップロードヘルパ

### 単一 subresource 用

```cpp
D3D12Resource CreateTexture2DFromMemory(
    D3D12Core& core,
    const void* data,
    UINT width, UINT height, DXGI_FORMAT format,
    UINT srcRowPitch = 0,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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
```

これらは単一 subresource の Texture2D 専用です。planar format、mipmap、array texture など複数 subresource を持つ texture には使えません。

### 複数 subresource 用

```cpp
struct D3D12TextureSubresourceData {
    const void* data = nullptr;
    UINT64 rowPitch = 0;
    UINT64 slicePitch = 0;
};

UINT64 GetRequiredUploadSize(D3D12Core& core, const D3D12Resource& dstTexture);
UINT64 GetRequiredUploadSize(
    D3D12Core& core,
    const D3D12Resource& dstTexture,
    UINT firstSubresource,
    UINT subresourceCount);

void RecordUploadTextureSubresources(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadBuffer& upload,
    const D3D12TextureSubresourceData* subresources,
    UINT firstSubresource,
    UINT subresourceCount,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

void RecordUploadTextureSubresources(
    D3D12Core& core,
    D3D12CommandContext& ctx,
    D3D12Resource& dstTexture,
    D3D12UploadRing& ring,
    const D3D12TextureSubresourceData* subresources,
    UINT firstSubresource,
    UINT subresourceCount,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
```

`rowPitch = 0` のときは `GetCopyableFootprints` の row size を使います。`slicePitch = 0` のときは `rowPitch * numRows` を使います。`dstTexture` は COPY_DEST 状態であることを前提にし、コピー後に全 subresource を `finalState` へ遷移します。

---

## ディスクリプタ作成ヘルパ

### full-desc view

```cpp
void CreateSrv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

void CreateUav(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    ID3D12Resource* counterResource = nullptr);

void CreateRtv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_RENDER_TARGET_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

void CreateDsv(
    D3D12Core& core,
    ID3D12Resource* resource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);
```

### 便利 view helper

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

`CreateBufferSrv` / `CreateBufferUav` は structured buffer と typed buffer の両方を扱えます。`format == DXGI_FORMAT_UNKNOWN` のときは structured buffer view、typed format を指定したときは typed buffer view です。Structured buffer view では `structureByteStride` を必ず明示してください。Typed buffer view では `structureByteStride` は `0` にしてください。

---

## Sampler helper

```cpp
D3D12_SAMPLER_DESC MakeLinearClampSamplerDesc();
D3D12_SAMPLER_DESC MakePointClampSamplerDesc();

void CreateSampler(
    D3D12Core& core,
    const D3D12_SAMPLER_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);
```

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
