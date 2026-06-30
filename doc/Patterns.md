# 使用パターン集（レシピ）

よくある処理の流れをコード断片で示します。完全に動くプログラムは [`../sample`](../sample) を参照してください。前提として `using namespace D3D12CoreLib;` を想定します。

- [1. 初期化と後片付け](#1-初期化と後片付け)
- [2. コマンドの記録・実行・待機](#2-コマンドの記録実行待機)
- [3. テクスチャを同期アップロード（初期化用）](#3-テクスチャを同期アップロード初期化用)
- [4. 高 fps アップロード（UploadRing）](#4-高-fps-アップロードuploadring)
- [5. Compute Shader を実行する](#5-compute-shader-を実行する)
- [6. GPU 結果を CPU に読み戻す（Readback）](#6-gpu-結果を-cpu-に読み戻すreadback)
- [7. キュー間同期（Copy → Direct）](#7-キュー間同期copy--direct)
- [8. 特定アダプタで初期化・共有リソース](#8-特定アダプタで初期化共有リソース)
- [9. リソース状態の手動管理](#9-リソース状態の手動管理)
- [10. スワップチェーンの描画ループ](#10-スワップチェーンの描画ループ)
- [11. 複数スレッドでのコマンド並列記録](#11-複数スレッドでのコマンド並列記録)
- [12. グラフィクスパイプラインの作成](#12-グラフィクスパイプラインの作成)

---

## 1. 初期化と後片付け

```cpp
D3D12CoreConfig config;
config.enableDebugLayer    = true;
config.enableGpuValidation = false;   // 重いので必要なときだけ
config.createComputeQueue  = true;    // 専用 Compute Queue が欲しい場合

auto core = D3D12Core::CreateShared(config);   // shared_ptr。サブシステムで共有

// ... 各種処理 ...

core->WaitIdle();   // 終了前に全キューをフラッシュしてから破棄
```

例外で受けるのが基本です。

```cpp
try {
    auto core = D3D12Core::CreateShared();
    // ...
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";   // HRESULT・式・ファイル・行を含む
}
```

---

## 2. コマンドの記録・実行・待機

`D3D12CommandContext` は生成直後 Close 状態。`Reset → 記録 → Close → Execute → Signal → Wait` の順です。

```cpp
D3D12CommandContext ctx = core->CreateDirectContext();

ctx.Reset();                                  // 記録開始
auto* cl = ctx.GetCommandList();
// cl->... でコマンドを積む
ctx.Close();                                  // 記録終了

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
UINT64 fv = core->DirectQueue().Signal();
core->DirectQueue().WaitForFenceValue(fv);    // この Allocator を再 Reset する前に必須
```

> `ctx.Reset()` は、その Allocator で積んだ GPU 処理が完了してから呼ぶ必要があります。上のように `WaitForFenceValue` で待ってから次フレームの `Reset()` を行ってください。

---

## 3. テクスチャを同期アップロード（初期化用）

CPU 上の RGBA8 配列から SRV 用テクスチャを作ります。内部でアップロード～GPU 完了待ちまで行うので、これ 1 行で使えるテクスチャが返ります。

```cpp
std::vector<uint8_t> rgba = LoadImageSomehow(&w, &h);   // 4 byte/px

D3D12Resource tex = CreateTexture2DFromRGBA(
    *core, rgba.data(), w, h,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // 戻り状態
```

RGB（3 byte/px）しか無い場合:

```cpp
D3D12Resource tex = CreateTexture2DFromRGB(*core, rgb.data(), w, h, /*alpha*/ 255);
```

SRV を作って使う:

```cpp
D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, /*shaderVisible*/ true);

auto srv = alloc.Allocate();
CreateTexture2DSrv(*core, tex, srv.cpu);
// 描画時に srv.gpu を SetGraphicsRootDescriptorTable する
```

---

## 4. 高 fps アップロード（UploadRing）

毎フレーム CPU からテクスチャを更新する場合は `UploadRing` を使い、待たずにコマンドだけ積みます。

```cpp
// 初期化（リングサイズ = 最大フレームサイズ × インフライト数 以上）
D3D12UploadRing ring;
ring.Initialize(core->GetDevice(), 64ull * 1024 * 1024);   // 64MB 例

D3D12Resource dst = CreateTexture2D(
    *core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

// --- 毎フレーム ---
ring.ReclaimCompleted(core->DirectQueue().Fence());   // 冒頭で空き回収

ctx.Reset();
RecordUploadTexture2D(*core, ctx, dst, ring, frameData, w, h,
                      DXGI_FORMAT_R8G8B8A8_UNORM);     // Wait しない
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
UINT64 fv = core->DirectQueue().Signal();
ring.FinishFrame(fv);                                  // 確保位置を Fence 値で記録
```

---

## 5. Compute Shader を実行する

テンプレート Root Signature を使った典型的な Compute（SRV 1 + UAV 1 + Root定数）。

```cpp
// 1) シェーダをコンパイル（SM 6.0 / DXC）
ShaderBytecode cs = CompileShaderFromSource_Dxc(hlsl, "main", "cs_6_0");

// 2) パイプライン（テンプレ RootSig 自動生成）
ComputePipelineDesc desc;
desc.numSrvs               = 1;   // t0
desc.numUavs               = 1;   // u0
desc.numRootConstantValues = 2;   // b0: 例として width, height

D3D12ComputePipeline pipeline;
pipeline.InitializeWithTemplate(core->GetDevice(), cs, desc);

// 3) 入出力テクスチャと SRV/UAV
D3D12Resource input  = CreateTexture2DFromRGBA(
    *core, rgba.data(), w, h,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);   // compute SRV 用の読み状態

D3D12Resource output = CreateTexture2D(
    *core, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);       // UAV フラグ必須

D3D12DescriptorAllocator alloc;
alloc.Initialize(core->GetDevice(),
                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, /*shaderVisible*/ true);
auto srv = alloc.Allocate();  CreateTexture2DSrv(*core, input,  srv.cpu);
auto uav = alloc.Allocate();  CreateTexture2DUav(*core, output, uav.cpu);

// 4) Dispatch
ctx.Reset();
auto* cl = ctx.GetCommandList();

ID3D12DescriptorHeap* heaps[] = { alloc.GetHeap() };
cl->SetDescriptorHeaps(1, heaps);
cl->SetComputeRootSignature(pipeline.GetRootSignature());
cl->SetPipelineState(pipeline.GetPipelineState());

cl->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srv.gpu);
cl->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), uav.gpu);
UINT consts[2] = { w, h };
cl->SetComputeRoot32BitConstants(pipeline.RootConstantsIndex(), 2, consts, 0);

cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);   // shader が [numthreads(8,8,1)] の場合
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());
```

対応する HLSL（抜粋）:

```hlsl
Texture2D<float4>   gInput  : register(t0);
RWTexture2D<float4> gOutput : register(u0);
cbuffer Constants : register(b0) { uint gWidth; uint gHeight; }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gWidth || id.y >= gHeight) return;
    float4 c = gInput.Load(int3(id.xy, 0));
    gOutput[id.xy] = c;   // ここで処理を行う
}
```

> **typed UAV load について:** 上の例は UAV へ「書き込む」だけ（typed store）なので `R8G8B8A8_UNORM` で問題ありません。シェーダ内で UAV を「読む」（typed load）必要がある場合は、`core->DeviceContext().SupportsTypedUavLoad(format)` で対応を確認してください。

---

## 6. GPU 結果を CPU に読み戻す（Readback）

Compute で書いた UAV テクスチャを CPU に戻す例。テクスチャ→バッファのコピーは行ピッチが 256 アラインされる点に注意します。

```cpp
// フットプリント取得
D3D12_RESOURCE_DESC od = output.GetDesc();
D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
UINT   rows = 0;
UINT64 rowSize = 0, total = 0;
core->GetDevice()->GetCopyableFootprints(&od, 0, 1, 0, &fp, &rows, &rowSize, &total);

D3D12ReadbackBuffer readback;
readback.Initialize(core->GetDevice(), total);

ctx.Reset();
auto* cl = ctx.GetCommandList();

// UAV → COPY_SOURCE
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
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());   // 完了待ち

// 読み出し（行ピッチに注意）
const auto* base = static_cast<const uint8_t*>(readback.Map());
for (UINT y = 0; y < rows; ++y) {
    const uint8_t* row = base + static_cast<size_t>(y) * fp.Footprint.RowPitch;
    // row[x*4 + 0..3] が RGBA
}
readback.Unmap();
```

---

## 7. キュー間同期（Copy → Direct）

Copy キューでアップロードしたリソースを、Direct キューが使う前に GPU 側で待つ例。

```cpp
// Copy キューでコピーを実行
copyCtx.Close();
ID3D12CommandList* copyLists[] = { copyCtx.GetCommandList() };
core->CopyQueue().ExecuteCommandLists(1, copyLists);
UINT64 copyFv = core->CopyQueue().Signal();

// Direct キューは copyFv の完了を GPU 側で待ってから実行
core->DirectQueue().GpuWait(core->CopyQueue().Fence().Get(), copyFv);

drawCtx.Close();
ID3D12CommandList* drawLists[] = { drawCtx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, drawLists);
core->DirectQueue().Signal();
```

`GpuWait` は CPU をブロックしません（GPU のスケジューラ上で待ちます）。

---

## 8. 特定アダプタで初期化・共有リソース

LUID を指定して特定 GPU で初期化し、他 API と共有するリソースのハンドルを作る例。

```cpp
LUID targetLuid = /* 他 API から取得した LUID */;
auto core = D3D12Core::CreateSharedWithAdapterLuid(targetLuid);

if (!core->DeviceContext().SupportsResourceSharing()) {
    throw std::runtime_error("this adapter does not support resource sharing");
}

// 共有リソースは D3D12_HEAP_FLAG_SHARED 付きで自前作成する必要がある
//（CreateBuffer/CreateTexture2D は HEAP_FLAG_NONE なので、共有用は直接 CreateCommittedResource する）

HANDLE shared = D3D12SharedResource::CreateSharedHandle(
    core->GetDevice(), sharedResource.Get());
// ... 他 API へ shared を渡す ...
CloseHandle(shared);   // 使い終わったら呼び出し側で解放
```

同一アダプタかの確認:

```cpp
if (core->IsSameAdapter(otherApiLuid)) { /* ゼロコピー共有が可能 */ }
```

---

## 9. リソース状態の手動管理

`D3D12Resource` の状態追跡は単一状態の簡易版です。mip / array で状態が分かれる、複数キューで同時参照する、外部 API と共有する、といったケースでは before/after を明示して手動管理します。

```cpp
// 追跡値に頼らず明示的に遷移
ctx.ResourceBarrier(MakeTransitionBarrier(
    tex.Get(),
    /*before*/ D3D12_RESOURCE_STATE_COPY_DEST,
    /*after*/  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    /*subresource*/ 0));            // 特定サブリソースだけ遷移

tex.SetState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // 追跡値も合わせる（任意）
```

同一 UAV への連続書き込みの間に同期が必要なら UAV バリア:

```cpp
ctx.ResourceBarrier(MakeUavBarrier(tex.Get()));
```

---

## 10. スワップチェーンの描画ループ

ウィンドウ表示の骨格。バックバッファ数ぶんの記録コンテキストを用意し、`PRESENT <-> RENDER_TARGET` を明示バリアで遷移します。完全な Win32 込みの例は [`../sample/03_HelloTriangle`](../sample/03_HelloTriangle) を参照。

```cpp
// 準備
ComPtr<IDXGISwapChain3> sc = CreateSwapChainForHwnd(*core, hwnd, w, h, bufferCount);

D3D12DescriptorAllocator rtvAlloc;
rtvAlloc.Initialize(core->GetDevice(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV, bufferCount, /*shaderVisible*/ false);

std::vector<D3D12Resource>               backBuffers(bufferCount);
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv(bufferCount);
std::vector<D3D12CommandContext>         ctxs(bufferCount);
std::vector<UINT64>                      frameFv(bufferCount, 0);
for (UINT i = 0; i < bufferCount; ++i) {
    backBuffers[i] = GetSwapChainBackBuffer(sc.Get(), i);
    auto h = rtvAlloc.Allocate();
    core->GetDevice()->CreateRenderTargetView(backBuffers[i].Get(), nullptr, h.cpu);
    rtv[i]  = h.cpu;
    ctxs[i] = core->CreateDirectContext();
}

// 毎フレーム
UINT idx = sc->GetCurrentBackBufferIndex();
core->DirectQueue().WaitForFenceValue(frameFv[idx]);   // このバッファの前回完了を待つ

D3D12CommandContext& ctx = ctxs[idx];
ctx.Reset();
ctx.ResourceBarrier(MakeTransitionBarrier(backBuffers[idx].Get(),
    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

float clear[4] = { 0.1f, 0.2f, 0.4f, 1.0f };
ctx.GetCommandList()->OMSetRenderTargets(1, &rtv[idx], FALSE, nullptr);
ctx.GetCommandList()->ClearRenderTargetView(rtv[idx], clear, 0, nullptr);
// ... ここで描画コマンド ...

ctx.ResourceBarrier(MakeTransitionBarrier(backBuffers[idx].Get(),
    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
ctx.Close();

ID3D12CommandList* lists[] = { ctx.GetCommandList() };
core->DirectQueue().ExecuteCommandLists(1, lists);
sc->Present(1, 0);                                     // VSync ON
frameFv[idx] = core->DirectQueue().Signal();           // このフレームの完了印
```

> `frameFv` を 0 で初期化しておくと、最初の `WaitForFenceValue(0)` は即座に返ります（Fence は 1 から Signal されるため）。

---

## 11. 複数スレッドでのコマンド並列記録

1 つの Core（device / queue）を共有しつつ、各スレッドが**自分のコマンドリストを並列に記録**し、main がまとめて実行する定石です。完全な例は [`../sample/04_ParallelCompute`](../sample/04_ParallelCompute)。

スレッド安全性の指針（ヘッダ記載に準拠）:

- `ID3D12Device` の生成系はスレッドセーフ。各スレッドが Context / Resource / View を作ってよい。
- `D3D12DescriptorAllocator` は**非スレッドセーフ**。スレッドごとに 1 つ持つ。
- 1 つのコマンドリストの記録は単一スレッドから（リストごとにスレッドを分ける）。
- `D3D12Fence::Signal` はキューを独占する 1 スレッド（= main）から。`IsCompleted` / `GetCurrentValue` の読みは別スレッドから可。

```cpp
// 各スレッドは自分の ctx / alloc / 出力リソースに対して記録する
auto record = [&](int i) {
    works[i].ctx.Reset();
    auto* cl = works[i].ctx.GetCommandList();
    // ... works[i] 専用の Descriptor / Resource を使ってコマンドを積む ...
    works[i].ctx.Close();
};

std::vector<std::thread> threads;
for (int i = 0; i < N; ++i) threads.emplace_back(record, i);
for (auto& t : threads) t.join();

// main がまとめて 1 回実行し、1 回だけ Signal して待つ
std::vector<ID3D12CommandList*> lists;
for (auto& w : works) lists.push_back(w.ctx.GetCommandList());
core->DirectQueue().ExecuteCommandLists((UINT)lists.size(), lists.data());
core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());
```

---

## 12. グラフィクスパイプラインの作成

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

// 既定 state を一部だけ上書きしたいとき（任意）
D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
gd.rasterizer = &noCull;       // noCull は Initialize まで生かす

D3D12GraphicsPipeline pipeline;
pipeline.Initialize(device, rootSig, gd);   // フル制御は InitializeRaw(device, rootSig, fullDesc)
```
