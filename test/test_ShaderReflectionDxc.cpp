//
// test_ShaderReflectionDxc.cpp - DXC/DXIL shader reflection hardening.
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12ShaderReflection.hpp>

using namespace D3D12CoreLib;

namespace {

const char* kDxcComputeShader = R"(
[numthreads(8,4,2)]
void main(uint3 id : SV_DispatchThreadID) {
}
)";

} // namespace

TEST(ShaderReflection, ReflectsDxcDxilComputeShader) {
    REQUIRE_DXC();
    ShaderBytecode bytecode = CompileShaderFromSource_Dxc(kDxcComputeShader, "main", "cs_6_0");
    ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);

    CHECK(reflection.threadGroupSizeX == 8);
    CHECK(reflection.threadGroupSizeY == 4);
    CHECK(reflection.threadGroupSizeZ == 2);
}
