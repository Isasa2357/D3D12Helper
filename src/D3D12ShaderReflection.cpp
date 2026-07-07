#include <D3D12Helper/D3D12Gpu/D3D12ShaderReflection.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <d3dcompiler.h>

#include <algorithm>
#include <stdexcept>

namespace D3D12CoreLib {
namespace {

std::string SafeString(const char* value) {
    return value ? std::string(value) : std::string();
}

UINT CountMaskComponents(BYTE mask) noexcept {
    UINT count = 0;
    if (mask & 0x1) ++count;
    if (mask & 0x2) ++count;
    if (mask & 0x4) ++count;
    if (mask & 0x8) ++count;
    return count;
}

ComPtr<ID3D12ShaderReflection> CreateReflection(const void* bytecode, size_t bytecodeSize) {
    if (!bytecode || bytecodeSize == 0) {
        throw std::runtime_error("ReflectShaderBytecode: empty shader bytecode");
    }

    ComPtr<ID3D12ShaderReflection> reflection;
    const HRESULT hr = D3DReflect(
        bytecode,
        bytecodeSize,
        IID_PPV_ARGS(&reflection));
    if (FAILED(hr) || !reflection) {
        throw std::runtime_error(
            "ReflectShaderBytecode: D3DReflect failed. "
            "The current helper supports DXBC bytecode produced by D3DCompile.");
    }
    return reflection;
}

ShaderSignatureParameterInfo ConvertSignatureParameter(const D3D12_SIGNATURE_PARAMETER_DESC& desc) {
    ShaderSignatureParameterInfo out;
    out.semanticName = SafeString(desc.SemanticName);
    out.semanticIndex = desc.SemanticIndex;
    out.registerIndex = desc.Register;
    out.systemValueType = desc.SystemValueType;
    out.componentType = desc.ComponentType;
    out.mask = desc.Mask;
    out.readWriteMask = desc.ReadWriteMask;
    out.stream = desc.Stream;
    out.minPrecision = desc.MinPrecision;
    return out;
}

} // namespace

DXGI_FORMAT MakeInputLayoutFormat(D3D_REGISTER_COMPONENT_TYPE componentType, BYTE mask) noexcept {
    const UINT count = CountMaskComponents(mask);
    if (count == 0 || count > 4) {
        return DXGI_FORMAT_UNKNOWN;
    }

    switch (componentType) {
    case D3D_REGISTER_COMPONENT_UINT32:
        switch (count) {
        case 1: return DXGI_FORMAT_R32_UINT;
        case 2: return DXGI_FORMAT_R32G32_UINT;
        case 3: return DXGI_FORMAT_R32G32B32_UINT;
        case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    case D3D_REGISTER_COMPONENT_SINT32:
        switch (count) {
        case 1: return DXGI_FORMAT_R32_SINT;
        case 2: return DXGI_FORMAT_R32G32_SINT;
        case 3: return DXGI_FORMAT_R32G32B32_SINT;
        case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    case D3D_REGISTER_COMPONENT_FLOAT32:
        switch (count) {
        case 1: return DXGI_FORMAT_R32_FLOAT;
        case 2: return DXGI_FORMAT_R32G32_FLOAT;
        case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

ShaderReflectionInfo ReflectShaderBytecode(const void* bytecode, size_t bytecodeSize) {
    auto reflection = CreateReflection(bytecode, bytecodeSize);

    D3D12_SHADER_DESC shaderDesc = {};
    D3D12CORE_THROW_IF_FAILED(reflection->GetDesc(&shaderDesc));

    ShaderReflectionInfo info;
    info.version = shaderDesc.Version;
    info.creator = SafeString(shaderDesc.Creator);
    info.flags = shaderDesc.Flags;
    info.instructionCount = shaderDesc.InstructionCount;
    info.boundResourceCount = shaderDesc.BoundResources;
    info.constantBufferCount = shaderDesc.ConstantBuffers;
    info.inputParameterCount = shaderDesc.InputParameters;
    info.outputParameterCount = shaderDesc.OutputParameters;

    reflection->GetThreadGroupSize(
        &info.threadGroupSizeX,
        &info.threadGroupSizeY,
        &info.threadGroupSizeZ);

    info.resources.reserve(shaderDesc.BoundResources);
    for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
        D3D12CORE_THROW_IF_FAILED(reflection->GetResourceBindingDesc(i, &bindDesc));

        ShaderResourceBindingInfo binding;
        binding.name = SafeString(bindDesc.Name);
        binding.type = bindDesc.Type;
        binding.returnType = bindDesc.ReturnType;
        binding.dimension = bindDesc.Dimension;
        binding.bindPoint = bindDesc.BindPoint;
        binding.bindCount = bindDesc.BindCount;
        binding.space = bindDesc.Space;
        binding.flags = bindDesc.uFlags;
        info.resources.push_back(std::move(binding));
    }

    info.constantBuffers.reserve(shaderDesc.ConstantBuffers);
    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i) {
        ID3D12ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);
        if (!cb) {
            continue;
        }

        D3D12_SHADER_BUFFER_DESC bufferDesc = {};
        D3D12CORE_THROW_IF_FAILED(cb->GetDesc(&bufferDesc));

        ShaderConstantBufferInfo cbInfo;
        cbInfo.name = SafeString(bufferDesc.Name);
        cbInfo.type = bufferDesc.Type;
        cbInfo.sizeBytes = bufferDesc.Size;
        cbInfo.flags = bufferDesc.uFlags;
        cbInfo.variables.reserve(bufferDesc.Variables);

        for (UINT v = 0; v < bufferDesc.Variables; ++v) {
            ID3D12ShaderReflectionVariable* variable = cb->GetVariableByIndex(v);
            if (!variable) {
                continue;
            }

            D3D12_SHADER_VARIABLE_DESC variableDesc = {};
            D3D12CORE_THROW_IF_FAILED(variable->GetDesc(&variableDesc));

            ShaderConstantBufferVariableInfo varInfo;
            varInfo.name = SafeString(variableDesc.Name);
            varInfo.startOffset = variableDesc.StartOffset;
            varInfo.sizeBytes = variableDesc.Size;
            varInfo.flags = variableDesc.uFlags;
            cbInfo.variables.push_back(std::move(varInfo));
        }

        info.constantBuffers.push_back(std::move(cbInfo));
    }

    info.inputParameters.reserve(shaderDesc.InputParameters);
    for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
        D3D12CORE_THROW_IF_FAILED(reflection->GetInputParameterDesc(i, &paramDesc));
        info.inputParameters.push_back(ConvertSignatureParameter(paramDesc));
    }

    info.outputParameters.reserve(shaderDesc.OutputParameters);
    for (UINT i = 0; i < shaderDesc.OutputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
        D3D12CORE_THROW_IF_FAILED(reflection->GetOutputParameterDesc(i, &paramDesc));
        info.outputParameters.push_back(ConvertSignatureParameter(paramDesc));
    }

    return info;
}

ShaderReflectionInfo ReflectShaderBytecode(const ShaderBytecode& bytecode) {
    return ReflectShaderBytecode(bytecode.Data(), bytecode.Size());
}

std::vector<ShaderInputLayoutElementInfo> MakeInputLayoutElementsFromReflection(
    const ShaderReflectionInfo& reflection,
    UINT inputSlot,
    D3D12_INPUT_CLASSIFICATION inputSlotClass,
    UINT instanceDataStepRate) {

    std::vector<ShaderInputLayoutElementInfo> elements;
    elements.reserve(reflection.inputParameters.size());

    for (const auto& param : reflection.inputParameters) {
        if (param.systemValueType != D3D_NAME_UNDEFINED) {
            continue;
        }

        ShaderInputLayoutElementInfo element;
        element.semanticName = param.semanticName;
        element.semanticIndex = param.semanticIndex;
        element.format = MakeInputLayoutFormat(param.componentType, param.mask);
        element.inputSlot = inputSlot;
        element.alignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        element.inputSlotClass = inputSlotClass;
        element.instanceDataStepRate = instanceDataStepRate;
        elements.push_back(std::move(element));
    }

    return elements;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> MakeD3D12InputElementDescs(
    const std::vector<ShaderInputLayoutElementInfo>& elements) {

    std::vector<D3D12_INPUT_ELEMENT_DESC> descs;
    descs.reserve(elements.size());

    for (const auto& element : elements) {
        D3D12_INPUT_ELEMENT_DESC desc = {};
        desc.SemanticName = element.semanticName.c_str();
        desc.SemanticIndex = element.semanticIndex;
        desc.Format = element.format;
        desc.InputSlot = element.inputSlot;
        desc.AlignedByteOffset = element.alignedByteOffset;
        desc.InputSlotClass = element.inputSlotClass;
        desc.InstanceDataStepRate = element.instanceDataStepRate;
        descs.push_back(desc);
    }

    return descs;
}

const ShaderResourceBindingInfo* FindResourceBinding(
    const ShaderReflectionInfo& reflection,
    const std::string& name) noexcept {

    auto it = std::find_if(reflection.resources.begin(), reflection.resources.end(),
        [&](const ShaderResourceBindingInfo& binding) { return binding.name == name; });
    return it == reflection.resources.end() ? nullptr : &(*it);
}

const ShaderConstantBufferInfo* FindConstantBuffer(
    const ShaderReflectionInfo& reflection,
    const std::string& name) noexcept {

    auto it = std::find_if(reflection.constantBuffers.begin(), reflection.constantBuffers.end(),
        [&](const ShaderConstantBufferInfo& cb) { return cb.name == name; });
    return it == reflection.constantBuffers.end() ? nullptr : &(*it);
}

const ShaderConstantBufferVariableInfo* FindConstantBufferVariable(
    const ShaderConstantBufferInfo& constantBuffer,
    const std::string& name) noexcept {

    auto it = std::find_if(constantBuffer.variables.begin(), constantBuffer.variables.end(),
        [&](const ShaderConstantBufferVariableInfo& variable) { return variable.name == name; });
    return it == constantBuffer.variables.end() ? nullptr : &(*it);
}

} // namespace D3D12CoreLib
