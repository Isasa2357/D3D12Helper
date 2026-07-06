#pragma once
//
// D3D12ComputePipeline.hpp
//
// Compute Shader 用の Root Signature + Pipeline State をまとめる。
//
// 想定する Root Signature の形（多くの compute shader で十分なテンプレ）:
//   - Root Parameter 0: SRV テーブル（t0 から numSrvs 個）
//   - Root Parameter 1: UAV テーブル（u0 から numUavs 個）
//   - Root Parameter 2: Root Constants（b0, numRootConstantValues 個の DWORD）
//
//   numXxx を 0 にすればその slot は省略される。
//   より複雑な Root Signature が必要なら、Initialize(rootSig, bytecode) を使う。
//
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ShaderCompiler.hpp>

namespace D3D12CoreLib {

struct ComputePipelineDesc {
    UINT numSrvs               = 0;
    UINT numUavs               = 0;
    UINT numRootConstantValues = 0; // b0 の DWORD 数。0 なら b0 は作らない。
};

class D3D12ComputePipeline {
public:
    D3D12ComputePipeline() = default;
    ~D3D12ComputePipeline() = default;

    D3D12ComputePipeline(const D3D12ComputePipeline&)            = delete;
    D3D12ComputePipeline& operator=(const D3D12ComputePipeline&) = delete;
    D3D12ComputePipeline(D3D12ComputePipeline&&)                 = default;
    D3D12ComputePipeline& operator=(D3D12ComputePipeline&&)      = default;

    // テンプレ Root Signature を自動生成して PSO を作る。
    void InitializeWithTemplate(ID3D12Device* device,
                                const ShaderBytecode& cs,
                                const ComputePipelineDesc& desc);

    // 自前の Root Signature と shader bytecode で初期化。
    void Initialize(ID3D12Device* device,
                    ComPtr<ID3D12RootSignature> rootSignature,
                    const ShaderBytecode& cs);

    // Root Signature と PSO を command list にセットする。
    void Bind(D3D12CommandContext& ctx) const;
    void Bind(ID3D12GraphicsCommandList* cmd) const;

    // Dispatch を発行する。Bind 済みであること、descriptor/root parameter 設定は呼び出し側責任。
    void Dispatch(D3D12CommandContext& ctx, UINT groupCountX, UINT groupCountY, UINT groupCountZ) const;
    void Dispatch(ID3D12GraphicsCommandList* cmd, UINT groupCountX, UINT groupCountY, UINT groupCountZ) const;

    ID3D12RootSignature*    GetRootSignature() const noexcept { return m_rootSig.Get(); }
    ID3D12PipelineState*    GetPipelineState() const noexcept { return m_pso.Get(); }

    // Root Parameter Index（テンプレ生成時のみ意味を持つ）
    UINT SrvTableIndex()    const noexcept { return m_srvTableIndex; }
    UINT UavTableIndex()    const noexcept { return m_uavTableIndex; }
    UINT RootConstantsIndex() const noexcept { return m_rootConstantsIndex; }

private:
    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;

    // テンプレ生成時のスロット番号（未使用なら UINT_MAX）
    UINT m_srvTableIndex      = UINT_MAX;
    UINT m_uavTableIndex      = UINT_MAX;
    UINT m_rootConstantsIndex = UINT_MAX;
};

} // namespace D3D12CoreLib
