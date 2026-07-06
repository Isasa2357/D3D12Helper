#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {

void D3D12ProcessingShaderCache::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_cache.clear();
}

const ShaderBytecode& D3D12ProcessingShaderCache::GetComputeShader(
    const std::string& fileName,
    const std::string& entryPoint,
    const std::vector<ShaderMacro>& defines) {

    if (!m_context) {
        throw ValidationError("D3D12ProcessingShaderCache::GetComputeShader: cache is not initialized");
    }

    const std::string key = MakeKey(fileName, entryPoint, defines);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        return it->second;
    }

    const auto path = m_context->ShaderDirectory() / fileName;
    if (!std::filesystem::exists(path)) {
        std::ostringstream os;
        os << "D3D12ProcessingShaderCache::GetComputeShader: shader file not found: " << path.string();
        throw ValidationError(os.str());
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::ostringstream os;
        os << "D3D12ProcessingShaderCache::GetComputeShader: cannot open shader file: " << path.string();
        throw ValidationError(os.str());
    }
    std::ostringstream sourceStream;
    sourceStream << ifs.rdbuf();
    if (!ifs.good() && !ifs.eof()) {
        std::ostringstream os;
        os << "D3D12ProcessingShaderCache::GetComputeShader: failed to read shader file: " << path.string();
        throw ValidationError(os.str());
    }
    const std::string source = sourceStream.str();

    ShaderCompileDesc desc = {};
    desc.sourcePath = path;
    desc.entryPoint = entryPoint;
    desc.includeDirs.push_back(m_context->ShaderDirectory());
    desc.defines = defines;

    try {
        desc.target = "cs_6_0";
        desc.useDxc = true;
        auto bytecode = CompileShaderFromSource(source, desc, fileName);
        auto inserted = m_cache.emplace(key, std::move(bytecode));
        return inserted.first->second;
    } catch (const std::exception& dxcError) {
        try {
            desc.target = "cs_5_0";
            desc.useDxc = false;
            auto bytecode = CompileShaderFromSource(source, desc, fileName);
            auto inserted = m_cache.emplace(key, std::move(bytecode));
            return inserted.first->second;
        } catch (const std::exception& d3dCompileError) {
            std::ostringstream os;
            os << "D3D12ProcessingShaderCache::GetComputeShader: failed to compile " << fileName
               << " with both DXC and D3DCompile. DXC: " << dxcError.what()
               << " D3DCompile: " << d3dCompileError.what();
            throw ValidationError(os.str());
        }
    }
}

void D3D12ProcessingShaderCache::Clear() {
    m_cache.clear();
}

std::string D3D12ProcessingShaderCache::MakeKey(
    const std::string& fileName,
    const std::string& entryPoint,
    const std::vector<ShaderMacro>& defines) const {

    std::ostringstream os;
    os << fileName << "|" << entryPoint;
    for (const auto& d : defines) {
        os << "|" << d.name << "=" << d.value;
    }
    return os.str();
}

} // namespace Processing
} // namespace D3D12CoreLib
