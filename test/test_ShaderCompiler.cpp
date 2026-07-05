//
// test_ShaderCompiler.cpp - シェーダのランタイムコンパイル
//
#include "TestCommon.hpp"

#include <filesystem>
#include <fstream>

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

void WriteTextFile(const std::filesystem::path& path, const char* text) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        TEST_FAIL(std::string("cannot create test shader file: ") + path.string());
    }
    ofs << text;
}
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

TEST(ShaderCompiler, CompileFromFileWithIncludeAndDefine) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "D3D12Helper_ShaderCompiler_Test";
    std::filesystem::create_directories(dir);

    const std::filesystem::path includePath = dir / "test_common.hlsli";
    const std::filesystem::path shaderPath = dir / "test_shader.hlsl";

    WriteTextFile(includePath, R"(
#ifndef TEST_VALUE
#define TEST_VALUE 1
#endif
uint MakeValue() { return TEST_VALUE; }
)");

    WriteTextFile(shaderPath, R"(
#include "test_common.hlsli"
RWStructuredBuffer<uint> gOut : register(u0);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) { gOut[id.x] = MakeValue(); }
)");

    ShaderCompileDesc desc;
    desc.sourcePath = shaderPath;
    desc.entryPoint = "main";
    desc.target = "cs_5_0";
    desc.includeDirs.push_back(dir);
    desc.defines.push_back({ "TEST_VALUE", "7" });
    desc.useDxc = false;

    ShaderBytecode bc = CompileShaderFromFile(desc);
    CHECK(!bc.Empty());
    CHECK(bc.AsD3D12().pShaderBytecode != nullptr);
}

TEST(ShaderCompiler, CompileFromFileThrowsOnMissingFile) {
    ShaderCompileDesc desc;
    desc.sourcePath = std::filesystem::temp_directory_path() / "D3D12Helper_missing_shader.hlsl";
    desc.entryPoint = "main";
    desc.target = "cs_5_0";
    CHECK_THROWS(CompileShaderFromFile(desc));
}
