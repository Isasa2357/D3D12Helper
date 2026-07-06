#pragma once
//
// D3D12ShaderCompiler.hpp
//
// シェーダバイトコードを取得する方法を提供:
//   1. LoadShaderBytecodeFromFile         : 事前コンパイル済み .cso をファイルから読む
//   2. CompileShaderFromSource_D3DCompile : D3DCompile (d3dcompiler) でランタイムコンパイル
//   3. CompileShaderFromSource_Dxc        : DXC でランタイムコンパイル
//   4. CompileShaderFromFile / Source     : includeDirs / defines を持つ汎用 compile API
//
// 戻り値の ShaderBytecode は所有権を持つ。D3D12_SHADER_BYTECODE は AsD3D12() で得る。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace D3D12CoreLib {

class ShaderBytecode {
public:
    ShaderBytecode() = default;
    explicit ShaderBytecode(std::vector<uint8_t> data) : m_data(std::move(data)) {}

    const void* Data() const noexcept { return m_data.data(); }
    size_t      Size() const noexcept { return m_data.size(); }
    bool        Empty() const noexcept { return m_data.empty(); }

    D3D12_SHADER_BYTECODE AsD3D12() const noexcept {
        return { m_data.data(), m_data.size() };
    }

private:
    std::vector<uint8_t> m_data;
};

struct ShaderMacro {
    std::string name;
    std::string value;
};

struct ShaderCompileDesc {
    std::filesystem::path sourcePath;
    std::string entryPoint;
    std::string target;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<ShaderMacro> defines;
    bool useDxc = false;
};

// .cso（コンパイル済みバイトコード）をファイルから読む。
ShaderBytecode LoadShaderBytecodeFromFile(const std::filesystem::path& csoPath);

// includeDirs / defines / useDxc を持つ汎用 compile API。
ShaderBytecode CompileShaderFromFile(const ShaderCompileDesc& desc);

ShaderBytecode CompileShaderFromSource(
    const std::string& hlslSource,
    const ShaderCompileDesc& desc,
    const std::string& sourceName = "shader");

// D3DCompile によるソースコンパイル。
//   target 例: "cs_5_1", "vs_5_1", "ps_5_1"
//   sourceName は #error 等の表示名。
ShaderBytecode CompileShaderFromSource_D3DCompile(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader");

// DXC によるソースコンパイル。
//   target 例: "cs_6_0", "cs_6_6"
//   dxcompiler.dll / dxil.dll が PATH に必要。
//   見つからなければ runtime_error を投げる。
ShaderBytecode CompileShaderFromSource_Dxc(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName = "shader",
    const std::vector<std::wstring>& extraArgs = {});

} // namespace D3D12CoreLib
