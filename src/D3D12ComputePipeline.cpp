//
// D3D12ComputePipeline.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <d3dcompiler.h>  // D3D12SerializeRootSignature
#include <stdexcept>
#include <utility>
#include <vector>

namespace D3D12CoreLib {

void D3D12ComputePipeline::InitializeWithTemplate(
    ID3D12Device* device,
    const ShaderBytecode& cs,
    const ComputePipelineDesc& desc) {

    if (!device) throw std::runtime_error("D3D12ComputePipeline: null device");
    if (cs.Empty()) throw std::runtime_error("D3D12ComputePipeline: empty shader bytecode");

    // 再初期化時に古い index が残らないようにする。
    m_srvTableIndex      = UINT_MAX;
    m_uavTableIndex      = UINT_MAX;
    m_rootConstantsIndex = UINT_MAX;

    // ----- Descriptor Ranges -----
    std::vector<D3D12_DESCRIPTOR_RANGE1> srvRanges;
    std::vector<D3D12_DESCRIPTOR_RANGE1> uavRanges;

    if (desc.numSrvs > 0) {
        D3D12_DESCRIPTOR_RANGE1 r = {};
        r.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        r.NumDescriptors                    = desc.numSrvs;
        r.BaseShaderRegister                = 0; // t0
        r.RegisterSpace                     = 0;
        r.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        srvRanges.push_back(r);
    }
    if (desc.numUavs > 0) {
        D3D12_DESCRIPTOR_RANGE1 r = {};
        r.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        r.NumDescriptors                    = desc.numUavs;
        r.BaseShaderRegister                = 0; // u0
        r.RegisterSpace                     = 0;
        r.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        uavRanges.push_back(r);
    }

    // ----- Root Parameters -----
    std::vector<D3D12_ROOT_PARAMETER1> params;

    if (!srvRanges.empty()) {
        D3D12_ROOT_PARAMETER1 p = {};
        p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(srvRanges.size());
        p.DescriptorTable.pDescriptorRanges   = srvRanges.data();
        p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        m_srvTableIndex = static_cast<UINT>(params.size());
        params.push_back(p);
    }
    if (!uavRanges.empty()) {
        D3D12_ROOT_PARAMETER1 p = {};
        p.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(uavRanges.size());
        p.DescriptorTable.pDescriptorRanges   = uavRanges.data();
        p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        m_uavTableIndex = static_cast<UINT>(params.size());
        params.push_back(p);
    }
    if (desc.numRootConstantValues > 0) {
        D3D12_ROOT_PARAMETER1 p = {};
        p.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0; // b0
        p.Constants.RegisterSpace  = 0;
        p.Constants.Num32BitValues = desc.numRootConstantValues;
        p.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        m_rootConstantsIndex = static_cast<UINT>(params.size());
        params.push_back(p);
    }

    // ----- Versioned Root Signature Desc -----
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.NumParameters     = static_cast<UINT>(params.size());
    versioned.Desc_1_1.pParameters       = params.empty() ? nullptr : params.data();
    versioned.Desc_1_1.NumStaticSamplers = 0;
    versioned.Desc_1_1.pStaticSamplers   = nullptr;
    versioned.Desc_1_1.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, &errors);
    if (FAILED(hr)) {
        std::string msg = "D3D12SerializeVersionedRootSignature failed";
        if (errors && errors->GetBufferSize() > 0) {
            msg += ": ";
            msg.append(reinterpret_cast<const char*>(errors->GetBufferPointer()),
                       errors->GetBufferSize());
        }
        throw std::runtime_error(msg);
    }

    D3D12CORE_THROW_IF_FAILED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)));

    // ----- Compute PSO -----
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSig.Get();
    psoDesc.CS             = cs.AsD3D12();
    psoDesc.NodeMask       = 0;

    D3D12CORE_THROW_IF_FAILED(
        device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));
}

void D3D12ComputePipeline::Initialize(
    ID3D12Device* device,
    ComPtr<ID3D12RootSignature> rootSignature,
    const ShaderBytecode& cs) {

    if (!device)        throw std::runtime_error("D3D12ComputePipeline: null device");
    if (!rootSignature) throw std::runtime_error("D3D12ComputePipeline: null root signature");
    if (cs.Empty())     throw std::runtime_error("D3D12ComputePipeline: empty bytecode");

    m_srvTableIndex      = UINT_MAX;
    m_uavTableIndex      = UINT_MAX;
    m_rootConstantsIndex = UINT_MAX;
    m_rootSig = std::move(rootSignature);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSig.Get();
    psoDesc.CS             = cs.AsD3D12();
    psoDesc.NodeMask       = 0;

    D3D12CORE_THROW_IF_FAILED(
        device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));
}

void D3D12ComputePipeline::Bind(D3D12CommandContext& ctx) const {
    Bind(ctx.GetCommandList());
}

void D3D12ComputePipeline::Bind(ID3D12GraphicsCommandList* cmd) const {
    if (!cmd) throw std::runtime_error("D3D12ComputePipeline::Bind: null command list");
    if (!m_rootSig) throw std::runtime_error("D3D12ComputePipeline::Bind: pipeline is not initialized");
    if (!m_pso) throw std::runtime_error("D3D12ComputePipeline::Bind: pipeline state is not initialized");
    cmd->SetComputeRootSignature(m_rootSig.Get());
    cmd->SetPipelineState(m_pso.Get());
}

void D3D12ComputePipeline::Dispatch(D3D12CommandContext& ctx,
                                    UINT groupCountX, UINT groupCountY, UINT groupCountZ) const {
    Dispatch(ctx.GetCommandList(), groupCountX, groupCountY, groupCountZ);
}

void D3D12ComputePipeline::Dispatch(ID3D12GraphicsCommandList* cmd,
                                    UINT groupCountX, UINT groupCountY, UINT groupCountZ) const {
    if (!cmd) throw std::runtime_error("D3D12ComputePipeline::Dispatch: null command list");
    if (!m_rootSig) throw std::runtime_error("D3D12ComputePipeline::Dispatch: pipeline is not initialized");
    if (!m_pso) throw std::runtime_error("D3D12ComputePipeline::Dispatch: pipeline state is not initialized");
    if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
        throw std::runtime_error("D3D12ComputePipeline::Dispatch: group counts must be > 0");
    }
    cmd->Dispatch(groupCountX, groupCountY, groupCountZ);
}

} // namespace D3D12CoreLib
