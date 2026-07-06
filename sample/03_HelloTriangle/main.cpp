//
// 03_HelloTriangle / main.cpp
//
// スワップチェーンを使ってウィンドウに三角形を描く、定番の "Hello Triangle"。
//
//   - Win32 ウィンドウ + スワップチェーン（FLIP_DISCARD）+ RTV
//   - DXC で頂点/ピクセルシェーダ（SM 6.0）をランタイムコンパイル
//   - グラフィクス PSO は D3D12GraphicsPipeline で構築（既定 state 自動 + 上書き可）
//   - 頂点バッファは D3D12UploadBuffer をそのまま流用（UPLOAD ヒープ / GENERIC_READ）
//   - 毎フレーム PRESENT <-> RENDER_TARGET を遷移し、クリア後に三角形を Draw
//
// PSO のカスタマイズは3段階:
//   1. かんたん   : GraphicsPipelineDesc に vs/ps/入力レイアウト/RTV だけ指定（state は既定）
//   2. 上書き     : desc.rasterizer/blend/depthStencil に PipelineDefaults や自前 desc を指す
//                   （本サンプルは単一三角形なのでカリング無効に上書きしている）
//   3. フル制御   : pipeline.InitializeRaw(device, rootSig, fullPsoDesc)
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>

#include <array>
#include <cstring>
#include <stdexcept>

using namespace D3D12CoreLib;

namespace {

constexpr UINT kWidth       = 1280;
constexpr UINT kHeight      = 720;
constexpr UINT kBufferCount = 2;
constexpr auto kRtvFormat   = DXGI_FORMAT_R8G8B8A8_UNORM;

bool g_quit = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            g_quit = true; PostQuitMessage(0); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { g_quit = true; PostQuitMessage(0); }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct Vertex {
    float position[3];
    float color[3];
};

const char* kTriangleHlsl = R"(
struct VSInput  { float3 pos : POSITION; float3 color : COLOR; };
struct PSInput  { float4 pos : SV_POSITION; float3 color : COLOR; };

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.pos   = float4(input.pos, 1.0f);
    o.color = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";

// 空の Root Signature（リソース未使用・IA 入力レイアウトは許可）。
// 実アプリでは CBV/SRV を持つ Root Signature をここで作る。
ComPtr<ID3D12RootSignature> CreateEmptyGraphicsRootSignature(ID3D12Device* device) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc = {};
    vdesc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    vdesc.Desc_1_1.NumParameters = 0;
    vdesc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&vdesc, &blob, &errors);
    if (FAILED(hr)) {
        std::string msg = "root signature serialize failed";
        if (errors) msg.append(static_cast<const char*>(errors->GetBufferPointer()),
                               errors->GetBufferSize());
        throw std::runtime_error(msg);
    }
    ComPtr<ID3D12RootSignature> rootSig;
    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    return rootSig;
}

} // namespace

int main() {
    try {
        // ---- ウィンドウ ----
        const wchar_t* kClassName = L"D3D12HelperHelloTriangle";
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        WNDCLASSW wc = {};
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = kClassName;
        wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&wc);

        RECT rc = { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        HWND hwnd = CreateWindowExW(
            0, kClassName, L"D3D12Helper - Hello Triangle (ESC to quit)",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);
        if (!hwnd) throw std::runtime_error("CreateWindowEx failed");
        ShowWindow(hwnd, SW_SHOW);

        // ---- Core / スワップチェーン ----
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        auto core = D3D12Core::CreateShared(config);
        ID3D12Device* device = core->GetDevice();

        ComPtr<IDXGISwapChain3> swapChain = CreateSwapChainForHwnd(
            *core, hwnd, kWidth, kHeight, kBufferCount, kRtvFormat);

        // ---- RTV ----
        D3D12DescriptorAllocator rtvAlloc;
        rtvAlloc.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                            kBufferCount, /*shaderVisible*/ false);

        std::array<D3D12Resource, kBufferCount>               backBuffers;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kBufferCount> rtvHandles{};
        for (UINT i = 0; i < kBufferCount; ++i) {
            backBuffers[i] = GetSwapChainBackBuffer(swapChain.Get(), i);
            D3D12DescriptorHandle h = rtvAlloc.Allocate();
            device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, h.cpu);
            rtvHandles[i] = h.cpu;
        }

        // ---- シェーダ・Root Signature・PSO ----
        ShaderBytecode vs = CompileShaderFromSource_Dxc(kTriangleHlsl, "VSMain", "vs_6_0");
        ShaderBytecode ps = CompileShaderFromSource_Dxc(kTriangleHlsl, "PSMain", "ps_6_0");
        ComPtr<ID3D12RootSignature> rootSig = CreateEmptyGraphicsRootSignature(device);

        GraphicsPipelineDesc gd;
        gd.vs          = std::move(vs);
        gd.ps          = std::move(ps);
        gd.inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        gd.rtvFormats[0] = kRtvFormat;   // 既定値も R8G8B8A8_UNORM だが明示

        // Tier 2（上書き）: 単一三角形なので裏面カリングを無効化する。
        D3D12_RASTERIZER_DESC noCull = PipelineDefaults::Rasterizer(D3D12_CULL_MODE_NONE);
        gd.rasterizer = &noCull;
        // 半透明にしたいなら: D3D12_BLEND_DESC ab = PipelineDefaults::BlendAlpha(); gd.blend = &ab;
        // 深度バッファを使うなら: gd.dsvFormat = DXGI_FORMAT_D32_FLOAT;（既定で深度テスト有効）

        D3D12GraphicsPipeline pipeline;
        pipeline.Initialize(device, rootSig, gd);
        // Tier 3（フル制御）が必要なら、自分で D3D12_GRAPHICS_PIPELINE_STATE_DESC を
        // 全部埋めて pipeline.InitializeRaw(device, rootSig, fullDesc) を呼ぶ。

        // ---- 頂点バッファ（UploadBuffer をそのまま流用）----
        const Vertex vertices[] = {
            { {  0.0f,  0.6f, 0.0f }, { 1.0f, 0.0f, 0.0f } },  // 上: 赤
            { {  0.6f, -0.6f, 0.0f }, { 0.0f, 1.0f, 0.0f } },  // 右下: 緑
            { { -0.6f, -0.6f, 0.0f }, { 0.0f, 0.0f, 1.0f } },  // 左下: 青
        };
        D3D12UploadBuffer vb;
        vb.Initialize(device, sizeof(vertices));
        std::memcpy(vb.Map(), vertices, sizeof(vertices));

        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = vb.Get()->GetGPUVirtualAddress();
        vbv.SizeInBytes    = sizeof(vertices);
        vbv.StrideInBytes  = sizeof(Vertex);

        // ---- フレーム同期 ----
        std::array<D3D12CommandContext, kBufferCount> contexts;
        std::array<UINT64, kBufferCount>              frameFenceValues{};
        for (auto& c : contexts) c = core->CreateDirectContext();

        const D3D12_VIEWPORT viewport = { 0.0f, 0.0f,
            static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f };
        const D3D12_RECT scissor = { 0, 0,
            static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };

        D3D12Queue& queue = core->DirectQueue();

        // ---- レンダリングループ ----
        while (!g_quit) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (g_quit) break;

            const UINT idx = swapChain->GetCurrentBackBufferIndex();
            queue.WaitForFenceValue(frameFenceValues[idx]);

            D3D12CommandContext& ctx = contexts[idx];
            ctx.Reset();
            auto* cl = ctx.GetCommandList();

            ctx.ResourceBarrier(MakeTransitionBarrier(
                backBuffers[idx].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET));

            const float clearColor[4] = { 0.07f, 0.09f, 0.12f, 1.0f };
            cl->OMSetRenderTargets(1, &rtvHandles[idx], FALSE, nullptr);
            cl->ClearRenderTargetView(rtvHandles[idx], clearColor, 0, nullptr);

            cl->RSSetViewports(1, &viewport);
            cl->RSSetScissorRects(1, &scissor);
            cl->SetGraphicsRootSignature(pipeline.GetRootSignature());
            cl->SetPipelineState(pipeline.GetPipelineState());
            cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cl->IASetVertexBuffers(0, 1, &vbv);
            cl->DrawInstanced(3, 1, 0, 0);

            ctx.ResourceBarrier(MakeTransitionBarrier(
                backBuffers[idx].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT));
            ctx.Close();

            ID3D12CommandList* lists[] = { ctx.GetCommandList() };
            queue.ExecuteCommandLists(1, lists);
            swapChain->Present(1, 0);
            frameFenceValues[idx] = queue.Signal();
        }

        core->WaitIdle();
        return 0;
    }
    catch (const std::exception& e) {
        std::string msg = std::string("Error: ") + e.what();
        MessageBoxA(nullptr, msg.c_str(), "03_HelloTriangle", MB_OK | MB_ICONERROR);
        return 1;
    }
}
