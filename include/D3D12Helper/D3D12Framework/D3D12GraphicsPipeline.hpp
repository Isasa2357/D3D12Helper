#pragma once
//
// D3D12GraphicsPipeline.hpp
//
// グラフィクス用の Root Signature + Pipeline State をまとめる。
// D3D12ComputePipeline と対称的に、次の3段のカスタマイズ経路を提供する:
//
//   1. かんたん経路   : GraphicsPipelineDesc に vs/ps/入力レイアウト/RTV だけ指定。
//                       blend / rasterizer / depth は未指定なら PipelineDefaults を使う。
//   2. 上書き経路     : GraphicsPipelineDesc の rasterizer/blend/depthStencil ポインタに
//                       自前 desc（または PipelineDefaults の戻り値）を指して細かく制御。
//                       MSAA・複数 RTV・DSV もここで指定する。
//   3. フル制御経路   : InitializeRaw に D3D12_GRAPHICS_PIPELINE_STATE_DESC を丸ごと渡す。
//                       ライブラリは PSO 生成と Root Signature 保持だけ行う。
//
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ShaderCompiler.hpp>   // ShaderBytecode

#include <vector>

namespace D3D12CoreLib {

// --------------------------------------------------------------------------
// よく使う state のプリセット。自分で全フィールドを埋めずに済むためのもの。
// 返り値は値型なので、必要なら受け取ってから一部だけ書き換えてもよい。
// --------------------------------------------------------------------------
namespace PipelineDefaults {

// ソリッド塗り。cull / fill / 表裏判定は引数で変えられる。
D3D12_RASTERIZER_DESC Rasterizer(
    D3D12_CULL_MODE cull = D3D12_CULL_MODE_BACK,
    D3D12_FILL_MODE fill = D3D12_FILL_MODE_SOLID,
    bool frontCounterClockwise = false);

// 不透明（ブレンド無し・全成分書き込み）。
D3D12_BLEND_DESC BlendOpaque();

// 標準的なアルファブレンド（src.a, 1-src.a / over 合成）。
D3D12_BLEND_DESC BlendAlpha();

// 深度・ステンシル無効。
D3D12_DEPTH_STENCIL_DESC DepthDisabled();

// 深度テスト有効（既定: 書き込み有り・LESS）。ステンシルは無効。
D3D12_DEPTH_STENCIL_DESC DepthDefault(
    bool depthWrite = true,
    D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS);

} // namespace PipelineDefaults

// --------------------------------------------------------------------------
// かんたん経路 / 上書き経路で使う記述子。
// rasterizer / blend / depthStencil が nullptr のときは PipelineDefaults を使う。
// --------------------------------------------------------------------------
struct GraphicsPipelineDesc {
    ShaderBytecode vs;
    ShaderBytecode ps;   // 空なら「ピクセルシェーダ無し」（深度のみ等）

    // 入力レイアウト。空なら SV_VertexID 等で頂点を生成するシェーダを想定。
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // レンダーターゲット
    UINT        numRenderTargets = 1;
    DXGI_FORMAT rtvFormats[8]    = { DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT dsvFormat        = DXGI_FORMAT_UNKNOWN;  // UNKNOWN なら深度添付無し

    // MSAA
    UINT sampleCount   = 1;
    UINT sampleQuality = 0;

    // state 上書き（nullptr のままなら既定値）。
    //   depthStencil 未指定時は dsvFormat の有無で DepthDefault / DepthDisabled を自動選択。
    const D3D12_RASTERIZER_DESC*    rasterizer   = nullptr;
    const D3D12_BLEND_DESC*         blend        = nullptr;
    const D3D12_DEPTH_STENCIL_DESC* depthStencil = nullptr;
};

class D3D12GraphicsPipeline {
public:
    D3D12GraphicsPipeline() = default;
    ~D3D12GraphicsPipeline() = default;

    D3D12GraphicsPipeline(const D3D12GraphicsPipeline&)            = delete;
    D3D12GraphicsPipeline& operator=(const D3D12GraphicsPipeline&) = delete;
    D3D12GraphicsPipeline(D3D12GraphicsPipeline&&)                 = default;
    D3D12GraphicsPipeline& operator=(D3D12GraphicsPipeline&&)      = default;

    // かんたん経路 / 上書き経路。desc から PSO を組む（未指定 state は既定値）。
    void Initialize(ID3D12Device* device,
                    ComPtr<ID3D12RootSignature> rootSignature,
                    const GraphicsPipelineDesc& desc);

    // フル制御経路。完成済みの PSO desc を渡す。pRootSignature は内部で
    // rootSignature 引数に差し替える（保持と一致させるため）。
    void InitializeRaw(ID3D12Device* device,
                       ComPtr<ID3D12RootSignature> rootSignature,
                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc);

    // Graphics Root Signature と PSO を command list にセットする。
    void Bind(D3D12CommandContext& ctx) const;
    void Bind(ID3D12GraphicsCommandList* cmd) const;

    ID3D12RootSignature* GetRootSignature() const noexcept { return m_rootSig.Get(); }
    ID3D12PipelineState* GetPipelineState() const noexcept { return m_pso.Get(); }

private:
    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace D3D12CoreLib
