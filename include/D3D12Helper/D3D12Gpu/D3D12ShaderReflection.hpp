#pragma once
//
// D3D12ShaderReflection.hpp
//
// Lightweight reflection helpers for compiled shader bytecode.
// The initial implementation focuses on DXBC bytecode produced by D3DCompile.
//
#include <D3D12Helper/D3D12Framework/D3D12ShaderCompiler.hpp>

#include <d3d12shader.h>

#include <cstdint>
#include <string>
#include <vector>

namespace D3D12CoreLib {

struct ShaderResourceBindingInfo {
    std::string name;
    D3D_SHADER_INPUT_TYPE type = D3D_SIT_CBUFFER;
    D3D_RESOURCE_RETURN_TYPE returnType = D3D_RETURN_TYPE_UNORM;
    D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
    UINT bindPoint = 0;
    UINT bindCount = 0;
    UINT space = 0;
    UINT flags = 0;
};

struct ShaderConstantBufferVariableInfo {
    std::string name;
    UINT startOffset = 0;
    UINT sizeBytes = 0;
    UINT flags = 0;
};

struct ShaderConstantBufferInfo {
    std::string name;
    D3D_CBUFFER_TYPE type = D3D_CT_CBUFFER;
    UINT sizeBytes = 0;
    UINT flags = 0;
    std::vector<ShaderConstantBufferVariableInfo> variables;
};

struct ShaderSignatureParameterInfo {
    std::string semanticName;
    UINT semanticIndex = 0;
    UINT registerIndex = 0;
    D3D_NAME systemValueType = D3D_NAME_UNDEFINED;
    D3D_REGISTER_COMPONENT_TYPE componentType = D3D_REGISTER_COMPONENT_UNKNOWN;
    BYTE mask = 0;
    BYTE readWriteMask = 0;
    UINT stream = 0;
    D3D_MIN_PRECISION minPrecision = D3D_MIN_PRECISION_DEFAULT;
};

struct ShaderInputLayoutElementInfo {
    std::string semanticName;
    UINT semanticIndex = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT inputSlot = 0;
    UINT alignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    UINT instanceDataStepRate = 0;
};

struct ShaderReflectionInfo {
    UINT version = 0;
    std::string creator;
    UINT flags = 0;
    UINT instructionCount = 0;
    UINT boundResourceCount = 0;
    UINT constantBufferCount = 0;
    UINT inputParameterCount = 0;
    UINT outputParameterCount = 0;
    UINT threadGroupSizeX = 0;
    UINT threadGroupSizeY = 0;
    UINT threadGroupSizeZ = 0;

    std::vector<ShaderResourceBindingInfo> resources;
    std::vector<ShaderConstantBufferInfo> constantBuffers;
    std::vector<ShaderSignatureParameterInfo> inputParameters;
    std::vector<ShaderSignatureParameterInfo> outputParameters;
};

ShaderReflectionInfo ReflectShaderBytecode(const void* bytecode, size_t bytecodeSize);
ShaderReflectionInfo ReflectShaderBytecode(const ShaderBytecode& bytecode);

DXGI_FORMAT MakeInputLayoutFormat(D3D_REGISTER_COMPONENT_TYPE componentType, BYTE mask) noexcept;

std::vector<ShaderInputLayoutElementInfo> MakeInputLayoutElementsFromReflection(
    const ShaderReflectionInfo& reflection,
    UINT inputSlot = 0,
    D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
    UINT instanceDataStepRate = 0);

// Returned D3D12_INPUT_ELEMENT_DESC objects reference semanticName strings owned
// by the input elements vector. Keep the elements vector alive while using the
// returned descriptors.
std::vector<D3D12_INPUT_ELEMENT_DESC> MakeD3D12InputElementDescs(
    const std::vector<ShaderInputLayoutElementInfo>& elements);

const ShaderResourceBindingInfo* FindResourceBinding(
    const ShaderReflectionInfo& reflection,
    const std::string& name) noexcept;

const ShaderConstantBufferInfo* FindConstantBuffer(
    const ShaderReflectionInfo& reflection,
    const std::string& name) noexcept;

const ShaderConstantBufferVariableInfo* FindConstantBufferVariable(
    const ShaderConstantBufferInfo& constantBuffer,
    const std::string& name) noexcept;

} // namespace D3D12CoreLib
