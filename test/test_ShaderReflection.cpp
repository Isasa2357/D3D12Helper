//
// test_ShaderReflection.cpp - Shader reflection helpers.
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12ShaderReflection.hpp>

using namespace D3D12CoreLib;

namespace {

const char* kVertexShader = R"(
cbuffer CameraConstants : register(b0)
{
    float4x4 viewProj;
};

struct VSIn {
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    uint instanceId : INSTANCE_ID;
};

struct VSOut {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOut main(VSIn input) {
    VSOut output;
    output.position = mul(float4(input.position, 1.0f), viewProj);
    output.texcoord = input.texcoord;
    return output;
}
)";

const char* kPixelShader = R"(
cbuffer MaterialConstants : register(b0)
{
    float4 tint;
    float gain;
};

Texture2D gTexture : register(t3, space2);
SamplerState gSampler : register(s1);

float4 main(float2 texcoord : TEXCOORD0) : SV_Target {
    return gTexture.Sample(gSampler, texcoord) * tint * gain;
}
)";

const char* kComputeShader = R"(
RWStructuredBuffer<uint> gOut : register(u0);
[numthreads(8,4,2)]
void main(uint3 id : SV_DispatchThreadID) {
    gOut[id.x] = id.x;
}
)";

} // namespace

TEST(ShaderReflection, ReflectsResourceBindingsAndConstantBuffers) {
    REQUIRE_DXC();
    ShaderBytecode bytecode = CompileShaderFromSource_Dxc(kPixelShader, "main", "ps_6_0");
    ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);

    CHECK(reflection.boundResourceCount >= 3);
    CHECK(reflection.inputParameterCount == 1);
    CHECK(reflection.outputParameterCount >= 1);

    const auto* cb = FindConstantBuffer(reflection, "MaterialConstants");
    CHECK(cb != nullptr);
    CHECK(cb->sizeBytes >= 20);
    CHECK(FindConstantBufferVariable(*cb, "tint") != nullptr);
    CHECK(FindConstantBufferVariable(*cb, "gain") != nullptr);

    const auto* texture = FindResourceBinding(reflection, "gTexture");
    CHECK(texture != nullptr);
    CHECK(texture->type == D3D_SIT_TEXTURE);
    CHECK(texture->bindPoint == 3);
    CHECK(texture->space == 2);

    const auto* sampler = FindResourceBinding(reflection, "gSampler");
    CHECK(sampler != nullptr);
    CHECK(sampler->type == D3D_SIT_SAMPLER);
    CHECK(sampler->bindPoint == 1);
}

TEST(ShaderReflection, BuildsInputLayoutElements) {
    ShaderBytecode bytecode = CompileShaderFromSource_D3DCompile(kVertexShader, "main", "vs_5_0");
    ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);

    auto elements = MakeInputLayoutElementsFromReflection(reflection);
    CHECK(elements.size() == 3);

    CHECK(elements[0].semanticName == "POSITION");
    CHECK(elements[0].format == DXGI_FORMAT_R32G32B32_FLOAT);

    CHECK(elements[1].semanticName == "TEXCOORD");
    CHECK(elements[1].format == DXGI_FORMAT_R32G32_FLOAT);

    CHECK(elements[2].semanticName == "INSTANCE_ID");
    CHECK(elements[2].format == DXGI_FORMAT_R32_UINT);

    auto d3dDescs = MakeD3D12InputElementDescs(elements);
    CHECK(d3dDescs.size() == elements.size());
    CHECK(d3dDescs[0].SemanticName != nullptr);
    CHECK(d3dDescs[0].AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT);
}

TEST(ShaderReflection, ReflectsComputeThreadGroupSize) {
    ShaderBytecode bytecode = CompileShaderFromSource_D3DCompile(kComputeShader, "main", "cs_5_0");
    ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);

    CHECK(reflection.threadGroupSizeX == 8);
    CHECK(reflection.threadGroupSizeY == 4);
    CHECK(reflection.threadGroupSizeZ == 2);

    const auto* out = FindResourceBinding(reflection, "gOut");
    CHECK(out != nullptr);
    CHECK(out->type == D3D_SIT_UAV_RWSTRUCTURED);
    CHECK(out->bindPoint == 0);
}

TEST(ShaderReflection, ThrowsOnEmptyBytecode) {
    CHECK_THROWS(ReflectShaderBytecode(nullptr, 0));
    ShaderBytecode empty;
    CHECK_THROWS(ReflectShaderBytecode(empty));
}

TEST(ShaderReflection, ReflectsDxcDxilComputeShader) {
    REQUIRE_DXC();
    const char* hlsl = R"(
RWStructuredBuffer<uint> gOut : register(u0);
[numthreads(2,3,4)]
void main(uint3 id : SV_DispatchThreadID) { gOut[id.x] = id.x; }
)";
    ShaderBytecode bytecode = CompileShaderFromSource_Dxc(hlsl, "main", "cs_6_0");
    ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);
    CHECK(reflection.threadGroupSizeX == 2);
    CHECK(reflection.threadGroupSizeY == 3);
    CHECK(reflection.threadGroupSizeZ == 4);
    const auto* out = FindResourceBinding(reflection, "gOut");
    CHECK(out != nullptr);
    CHECK(out->type == D3D_SIT_UAV_RWSTRUCTURED);
    CHECK(out->bindPoint == 0);
}
