#pragma once
//
// D3D12ProcessingShaderCache.hpp
// Runtime shader compiler/cache for Processing Layer HLSL files.
//
#include "D3D12ProcessingContext.hpp"
#include "../D3D12Framework/D3D12ShaderCompiler.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace D3D12CoreLib {
namespace Processing {

class D3D12ProcessingShaderCache {
public:
    void Initialize(D3D12ProcessingContext& context);

    const ShaderBytecode& GetComputeShader(
        const std::string& fileName,
        const std::string& entryPoint = "main",
        const std::vector<ShaderMacro>& defines = {});

    void Clear();

private:
    std::string MakeKey(const std::string& fileName,
                        const std::string& entryPoint,
                        const std::vector<ShaderMacro>& defines) const;

    D3D12ProcessingContext* m_context = nullptr;
    std::unordered_map<std::string, ShaderBytecode> m_cache;
};

} // namespace Processing
} // namespace D3D12CoreLib
