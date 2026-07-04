//
// test_ShaderCompiler.cpp - シェーダのランタイムコンパイル
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

namespace {
const char* kValidCs = R"(
RWStructuredBuffer<float> gOut : register(u0);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) { gOut[id.x] = 1.0f; }
)";

const char* kBrokenCs = R"(
[numthreads(1,1,1)]
void main() { this is not valid hlsl @@@ }
)";
} // namespace

TEST(ShaderCompiler, DxcCompilesValid) {
    REQUIRE_DXC();
    ShaderBytecode bc = CompileShaderFromSource_Dxc(kValidCs, "main", "cs_6_0");
    CHECK(!bc.Empty());
    CHECK(bc.Size() > 0);
    D3D12_SHADER_BYTECODE sb = bc.AsD3D12();
    CHECK(sb.pShaderBytecode != nullptr);
    CHECK(sb.BytecodeLength == bc.Size());
}

TEST(ShaderCompiler, DxcThrowsOnError) {
    REQUIRE_DXC();
    CHECK_THROWS(CompileShaderFromSource_Dxc(kBrokenCs, "main", "cs_6_0"));
}

TEST(ShaderCompiler, D3DCompileCompilesValid) {
    // d3dcompiler_47.dll は OS 同梱なので通常はスキップ不要。
    ShaderBytecode bc = CompileShaderFromSource_D3DCompile(kValidCs, "main", "cs_5_0");
    CHECK(!bc.Empty());
    CHECK(bc.AsD3D12().pShaderBytecode != nullptr);
}

TEST(ShaderCompiler, D3DCompileThrowsOnError) {
    CHECK_THROWS(CompileShaderFromSource_D3DCompile(kBrokenCs, "main", "cs_5_0"));
}
