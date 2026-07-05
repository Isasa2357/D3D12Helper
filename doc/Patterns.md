# 使用パターン集（レシピ）

よくある処理の流れをコード断片で示します。完全に動くプログラムは [`../sample`](../sample) を参照してください。前提として `using namespace D3D12CoreLib;` を想定します。

- [1. 初期化と後片付け](#1-初期化と後片付け)
- [2. コマンドの記録・実行・待機](#2-コマンドの記録実行待機)
- [3. テクスチャを同期アップロード（初期化用）](#3-テクスチャを同期アップロード初期化用)
- [4. 高 fps アップロード（UploadRing）](#4-高-fps-アップロードuploadring)
- [5. Compute Shader を実行する](#5-compute-shader-を実行する)
- [6. GPU 結果を CPU に読み戻す（Readback）](#6-gpu-結果を-cpu-に読み戻すreadback)
- [7. Buffer SRV/UAV/CBV を作る](#7-buffer-srvuavcbv-を作る)
- [8. 共有可能 Texture2D を作る](#8-共有可能-texture2d-を作る)
- [9. キュー間同期（Copy → Direct）](#9-キュー間同期copy--direct)
- [10. リソース状態の手動管理](#10-リソース状態の手動管理)
- [11. スワップチェーンの描画ループ](#11-スワップチェーンの描画ループ)
- [12. 複数スレッドでのコマンド並列記録](#12-複数スレッドでのコマンド並列記録)
- [13. グラフィクスパイプラインの作成](#13-グラフィクスパイプラインの作成)

---

## 1. 初期化と後片付け

```cpp
D3D12CoreConfig config;
config.enableDebugLayer    = true;
config.enableGpuValidation = false;
config.allowWarpAdapter    = true;
config.createComputeQueue  = true;

auto core = D3D12Core::CreateShared(config);

// ... 各種処理 ...

core->WaitIdle();
```

例外で受けるのが基本です。

```cpp
try {
    auto core = D3D12Core::CreateShared();
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
}
```

---

## 2. コマンドの記録・実行・待機

`D3D12CommandContext` は生成直後 Close 状態です。`Reset → 記録 → Close → Execute → Signal → Wait` の順です。

```cpp
D3D12CommandContext ctx = core->CreateDirectContext();

ctx.Reset();
auto* cl = ctx.GetCommandList();
// cl->... でコマンドを積む
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
UINT64 fv = core->DirectQueue().Signal();
core->DirectQueue().WaitForFenceValue(fv);
```

`ctx.Reset()` は、その Allocator で積んだ GPU 処理が完了してから呼ぶ必要があります。

---

## 3. テクスチャを同期アップロード（初期化用）

CPU 上の RGBA8 配列から SRV 用テクスチャを作ります。内部でアップロード～GPU 完了待ちまで行うので、これ 1 行で使えるテクスチャが返ります。

```cpp
std::vector<uint8_t> rgba = LoadImageSomehow(&w, &h);

D3D12Resource tex = CreateTexture2DFromRGBA(
    *core, rgba.data(), w, h,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
```

SRV を作って使う:

```cpp
D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, true);

auto srv = alloc.Allocate();
CreateTexture2DSrv(*core, tex, srv.cpu);
```

---

## 4. 高 fps アップロード（UploadRing）

毎フレーム CPU からテクスチャを更新する場合は `UploadRing` を使い、待たずにコマンドだけ積みます。

```cpp
D3D12UploadRing ring;
ring.Initialize(core->GetDevice(), 64ull * 1024 * 1024);

D3D12Resource dst = CreateTexture2D(
    *core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// --- 毎フレーム ---
ring.ReclaimCompleted(core->DirectQueue().Fence());

ctx.Reset();
RecordUploadTexture2D(*core, ctx, dst, ring, frameData, w, h,
                      DXGI_FORMAT_R8G8B8A8_UNORM);
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
UINT64 fv = core->DirectQueue().Signal();
ring.FinishFrame(fv);
```

---

## 5. Compute Shader を実行する

テンプレート Root Signature を使った典型的な Compute（SRV 1 + UAV 1 + Root定数）。

```cpp
ShaderBytecode cs = CompileShaderFromSource_Dxc(hlsl, "main", "cs_6_0");

ComputePipelineDesc desc;
desc.numSrvs               = 1;
desc.numUavs               = 1;
desc.numRootConstantValues = 2;

D3D12ComputePipeline pipeline;
pipeline.InitializeWithTemplate(core->GetDevice(), cs, desc);

D3D12Resource input  = CreateTexture2DFromRGBA(
    *core, rgba.data(), w, h,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

D3D12Resource output = CreateTexture2D(
    *core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, true);

auto srv = alloc.Allocate();
CreateTexture2DSrv(*core, input, srv.cpu);

auto uav = alloc.Allocate();
CreateTexture2DUav(*core, output, uav.cpu);

ctx.Reset();
auto* cl = ctx.GetCommandList();

ID3D12DescriptorHeap* heaps[] = { alloc.GetHeap() };
cl->SetDescriptorHeaps(1, heaps);

pipeline.Bind(ctx);

cl->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srv.gpu);
cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
UINT consts[2] = { w, h };
cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 2, consts, 0);

pipeline.Dispatch(ctx, (w + 7) / 8, (h + 7) / 8, 1);
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());
```

`Bind` は Root Signature と PSO だけを設定します。Descriptor Heap、Root Descriptor Table、Root Constants は呼び出し側で設定します。

---

## 6. GPU 結果を CPU に読み戻す（Readback）

Compute で書いた UAV テクスチャを CPU に戻す例。テクスチャ→バッファのコピーは行ピッチが 256 アラインされる点に注意します。

```cpp
D3D12_RESOURCE_DESC od = output.GetDesc();
D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
UINT   rows = 0;
UINT64 rowSize = 0, total = 0;
core->GetDevice()->GetCopyableFootprints(&od, 0, 1, 0, &fp, &rows, &rowSize, &total);

D3D12ReadbackBuffer readback;
readback.Initialize(core->GetDevice(), total);

ctx.Reset();
auto* cl = ctx.GetCommandList();

ctx.ResourceBarrier(MakeTransitionBarrier(
    output.Get(),
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_COPY_SOURCE));

D3D12_TEXTURE_COPY_LOCATION dst{};
dst.pResource       = readback.Get();
dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
dst.PlacedFootprint = fp;

D3D12_TEXTURE_COPY_LOCATION src{};
src.pResource        = output.Get();
src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
src.SubresourceIndex = 0;

cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

const auto* base = static_cast<const uint8_t*>(readback.Map());
for (UINT y = 0; y < rows; ++y) {
    const uint8_t* row = base + static_cast<size_t>(y) * fp.Footprint.RowPitch;
    // row[x*4 + 0..3] が RGBA
}
readback.Unmap();
```

---

## 7. Buffer SRV/UAV/CBV を作る

Structured Buffer を SRV/UAV として使う例です。

```cpp
constexpr UINT count  = 1024;
constexpr UINT stride = sizeof(float);

D3D12Resource input = CreateStructuredBuffer(
    *core,
    count,
    stride,
    D3D12_HEAP_TYPE_DEFAULT,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    D3D12_RESOURCE_FLAG_NONE);

D3D12Resource output = CreateStructuredBuffer(
    *core,
    count,
    stride,
    D3D12_HEAP_TYPE_DEFAULT,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);

auto srv = alloc.Allocate();
CreateBufferSrv(*core, input, srv.cpu, 0, count, stride);

auto uav = alloc.Allocate();
CreateBufferUav(*core, output, uav.cpu, 0, count, stride);
```

Constant Buffer は 256 byte アラインされます。

```cpp
struct Constants { float scale; UINT count; float pad[2]; };

D3D12Resource cb = CreateConstantBuffer(*core, sizeof(Constants));
void* mapped = nullptr;
cb.Get()->Map(0, nullptr, &mapped);
std::memcpy(mapped, &constants, sizeof(constants));
cb.Get()->Unmap(0, nullptr);

auto cbv = alloc.Allocate();
CreateConstantBufferView(*core, cb, cbv.cpu);
```

---

## 8. 共有可能 Texture2D を作る

D3D12 共有ハンドルを作るには、リソース作成時に `D3D12_HEAP_FLAG_SHARED` が必要です。通常の `CreateTexture2D` でも末尾の `heapFlags` に指定できますが、Texture2D 用には `CreateSharedTexture2D` を使うのが簡単です。

```cpp
auto core = D3D12Core::CreateShared();

if (!core->DeviceContext().SupportsResourceSharing()) {
    throw std::runtime_error("this adapter does not support resource sharing");
}

D3D12Resource sharedTex = CreateSharedTexture2D(
    *core,
    1920, 1080,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_COMMON,
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

HANDLE shared = D3D12SharedResource::CreateSharedHandle(
    core->GetDevice(), sharedTex.Get());

// ... 他 API / 他デバイスへ shared を渡す ...

CloseHandle(shared);
```

特定アダプタで初期化したい場合は LUID を指定します。

```cpp
LUID targetLuid = /* 他 API から取得した LUID */;
auto core = D3D12Core::CreateSharedWithAdapterLuid(targetLuid);
```

---

## 9. キュー間同期（Copy → Direct）

Copy キューでアップロードしたリソースを、Direct キューが使う前に GPU 側で待つ例です。

```cpp
copyCtx.Close();
ID3D12CommandList* copyLists[] = { copyCtx.GetCommandList() };
core->CopyQueue().ExecuteCommandLists(1, copyLists);
UINT64 copyFv = core->CopyQueue().Signal();

core->DirectQueue().GpuWait(core->CopyQueue().Fence().Get(), copyFv);

drawCtx.Close();
ID3D12CommandList* drawLists[] = { drawCtx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, drawLists);
core->DirectQueue().Signal();
```

`GpuWait` は CPU をブロックしません。

---

## 10. リソース状態の手動管理

`D3D12Resource` の状態追跡は単一状態の簡易版です。mip / array で状態が分かれる、複数キューで同時参照する、外部 API と共有する、といったケースでは before / after を明示して手動管理します。

```cpp
ctx.ResourceBarrier(MakeTransitionBarrier(
    tex.Get(),
    D3D12_RESOURCE_STATE_COPY_DEST,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    0));

tex.SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
```

同一 UAV への連続書き込みの間に同期が必要なら UAV バリアを使います。

```cpp
ctx.ResourceBarrier(MakeUavBarrier(tex.Get()));
```

---

## 11. スワップチェーンの描画ループ

ウィンドウ表示の骨格です。バックバッファ数ぶんの記録コンテキストを用意し、`PRESENT <-> RENDER_TARGET` を明示バリアで遷移します。完全な Win32 込みの例は [`../sample/03_HelloTriangle`](../sample/03_HelloTriangle) を参照してください。

```cpp
ComPtr<IDXGISwapChain3> sc = CreateSwapChainForHwnd(*core, hwnd, w, h, bufferCount);

D3D12DescriptorAllocator rtvAlloc;
rtvAlloc.Initialize(core->GetDevice(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV, bufferCount, false);

std::vector<D3D12Resource> backBuffers(bufferCount);
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv(bufferCount);
std::vector<D3D12CommandContext> ctxs(bufferCount);
std::vector<UINT64> frameFv(bufferCount, 0);

for (UINT i = 0; i < bufferCount; ++i) {
    backBuffers[i] = GetSwapChainBackBuffer(sc.Get(), i);
    auto h = rtvAlloc.Allocate();
    CreateTexture2DRtv(*core, backBuffers[i], h.cpu);
    rtv[i]  = h.cpu;
    ctxs[i] = core->CreateDirectContext();
}

UINT idx = sc->GetCurrentBackBufferIndex();
core->DirectQueue().WaitForFenceValue(frameFv[idx]);

D3D12CommandContext& ctx = ctxs[idx];
ctx.Reset();
ctx.ResourceBarrier(MakeTransitionBarrier(backBuffers[idx].Get(),
    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

float clear[4] = { 0.1f, 0.2f, 0.4f, 1.0f };
ctx.GetCommandList()->OMSetRenderTargets(1, &rtv[idx], FALSE, nullptr);
ctx.GetCommandList()->ClearRenderTargetView(rtv[idx], clear, 0, nullptr);

ctx.ResourceBarrier(MakeTransitionBarrier(backBuffers[idx].Get(),
    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
sc->Present(1, 0);
frameFv[idx] = core->DirectQueue().Signal();
```

---

## 12. 複数スレッドでのコマンド並列記録

1 つの Core（device / queue）を共有しつつ、各スレッドが自分のコマンドリストを並列に記録し、main がまとめて実行する定石です。完全な例は [`../sample/04_ParallelCompute`](../sample/04_ParallelCompute)。

- `ID3D12Device` の生成系はスレッドセーフ。各スレッドが Context / Resource / View を作ってよい。
- `D3D12DescriptorAllocator` は非スレッドセーフ。スレッドごとに 1 つ持つ。
- 1 つのコマンドリストの記録は単一スレッドから。
- `D3D12Fence::Signal` はキューを独占する 1 スレッドから。

```cpp
auto record = [&](int i) {
    works[i].ctx.Reset();
    auto* cl = works[i].ctx.GetCommandList();
    // ... works[i] 専用の Descriptor / Resource を使ってコマンドを積む ...
    works[i].ctx.Close();
};

std::vector<std::thread> threads;
for (int i = 0; i < N; ++i) threads.emplace_back(record, i);
for (auto& t : threads) t.join();

std::vector<ID3D12CommandList*> lists;
for (auto& w : works) lists.push_back(w.ctx.GetCommandList());
core->DirectQueue().ExecuteCommandLists((UINT)lists.size(), lists.data());
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());
```

---

## 13. グラフィクスパイプラインの作成

`D3D12GraphicsPipeline` は既定 state で簡単に PSO を作り、必要なら一部だけ上書きできます。Root Signature はアプリ側で作って渡します。完全な例は [`../sample/03_HelloTriangle`](../sample/03_HelloTriangle)。

```cpp
ShaderBytecode vs = CompileShaderFromSource_Dxc(hlsl, "VSMain", "vs_6_0");
ShaderBytecode ps = CompileShaderFromSource_Dxc(hlsl, "PSMain", "ps_6_0");

GraphicsPipelineDesc gd;
gd.vs = std::move(vs);
gd.ps = std::move(ps);
gd.inputLayout = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
gd.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
gd.rasterizer = &noCull;

D3D12GraphicsPipeline pipeline;
pipeline.Initialize(device, rootSig, gd);

ctx.Reset();
pipeline.Bind(ctx);
ctx.GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
// ... 頂点バッファ等をバインドして Draw ...
ctx.Close();
```
