//
// D3D12ShaderCompiler.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12ShaderCompiler.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <d3dcompiler.h>
#include <dxcapi.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace D3D12CoreLib {

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::ostringstream oss;
        oss << "ReadTextFile: cannot open " << path.string();
        throw std::runtime_error(oss.str());
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    if (!ifs.good() && !ifs.eof()) {
        std::ostringstream msg;
        msg << "ReadTextFile: read failed " << path.string();
        throw std::runtime_error(msg.str());
    }
    return oss.str();
}

std::vector<std::filesystem::path> BuildIncludeDirs(const ShaderCompileDesc& desc) {
    std::vector<std::filesystem::path> dirs;
    if (!desc.sourcePath.empty() && desc.sourcePath.has_parent_path()) {
        dirs.push_back(desc.sourcePath.parent_path());
    }
    for (const auto& d : desc.includeDirs) {
        if (!d.empty()) dirs.push_back(d);
    }
    return dirs;
}

std::string MakeCompileErrorMessage(const char* prefix, ID3DBlob* errors) {
    std::string msg = prefix;
    if (errors && errors->GetBufferSize() > 0) {
        msg += ": ";
        msg.append(reinterpret_cast<const char*>(errors->GetBufferPointer()),
                   errors->GetBufferSize());
    }
    return msg;
}

UINT MakeCompileFlags() {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    return flags;
}

std::string NormalizeD3DCompileTarget(const std::string& target) {
    // FXC can accept shader model 5.1, but some D3D12 runtime/driver combinations
    // reject the resulting graphics PSO while the same shader compiled as 5.0 works.
    // Keep DXC paths untouched; this normalization is for D3DCompile/FXC only.
    if (target == "vs_5_1") return "vs_5_0";
    if (target == "ps_5_1") return "ps_5_0";
    if (target == "gs_5_1") return "gs_5_0";
    if (target == "hs_5_1") return "hs_5_0";
    if (target == "ds_5_1") return "ds_5_0";
    if (target == "cs_5_1") return "cs_5_0";
    return target;
}

bool IsPixelShaderTarget(const std::string& target) {
    return target.rfind("ps_", 0) == 0;
}

bool IsTokenChar(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

bool IsTokenAt(const std::string& text, std::size_t pos, const char* token) {
    const std::size_t tokenLen = std::strlen(token);
    if (pos + tokenLen > text.size()) {
        return false;
    }
    if (text.compare(pos, tokenLen, token) != 0) {
        return false;
    }
    const bool beforeOk = pos == 0 || !IsTokenChar(text[pos - 1]);
    const bool afterOk = pos + tokenLen >= text.size() || !IsTokenChar(text[pos + tokenLen]);
    return beforeOk && afterOk;
}

bool LooksLikeLegacyTexcoordPixelMain(const std::string& source) {
    // Detect the legacy pixel-shader entry shape used by VarjoXR user shaders:
    //     float4 main(float2 uv : TEXCOORD0) : SV_TARGET
    // D3D12 can assign TEXCOORD to a different hardware register than the VS output
    // when the PS has only a standalone TEXCOORD parameter. We only wrap this old
    // one-parameter form; struct-based PS inputs are left untouched.
    std::size_t pos = 0;
    while ((pos = source.find("main", pos)) != std::string::npos) {
        if (!IsTokenAt(source, pos, "main")) {
            pos += 4;
            continue;
        }
        const std::size_t open = source.find('(', pos + 4);
        if (open == std::string::npos || open - pos > 64) {
            pos += 4;
            continue;
        }
        const std::size_t close = source.find(')', open + 1);
        if (close == std::string::npos) {
            return false;
        }
        const std::string params = source.substr(open + 1, close - open - 1);
        if (params.find(',') == std::string::npos &&
            params.find("float2") != std::string::npos &&
            params.find("TEXCOORD") != std::string::npos) {
            return true;
        }
        pos = close + 1;
    }
    return false;
}

std::string WrapLegacyTexcoordPixelShaderForD3D12(const std::string& hlslSource,
                                                  const std::string& normalizedTarget) {
    if (!IsPixelShaderTarget(normalizedTarget)) {
        return hlslSource;
    }
    if (!LooksLikeLegacyTexcoordPixelMain(hlslSource)) {
        return hlslSource;
    }

    std::ostringstream oss;
    oss << "// D3D12Helper compatibility wrapper for legacy PS main(float2 uv : TEXCOORD0).\n";
    oss << "// The real D3D12 PS entry receives SV_POSITION and TEXCOORD0 so VS/PS\n";
    oss << "// hardware register assignment stays compatible.\n";
    oss << "#define main D3D12Helper_LegacyPixelMain\n";
    oss << hlslSource << "\n";
    oss << "#undef main\n";
    oss << "struct D3D12Helper_LegacyPSInput\n";
    oss << "{\n";
    oss << "    float4 position : SV_POSITION;\n";
    oss << "    float2 uv : TEXCOORD0;\n";
    oss << "};\n";
    oss << "float4 main(D3D12Helper_LegacyPSInput input) : SV_TARGET\n";
    oss << "{\n";
    oss << "    return D3D12Helper_LegacyPixelMain(input.uv);\n";
    oss << "}\n";
    return oss.str();
}

std::vector<D3D_SHADER_MACRO> MakeD3DCompileMacros(const std::vector<ShaderMacro>& defines) {
    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(defines.size() + 1);
    for (const ShaderMacro& m : defines) {
        if (m.name.empty()) {
            throw std::runtime_error("ShaderCompileDesc: define name must not be empty");
        }
        macros.push_back({ m.name.c_str(), m.value.c_str() });
    }
    macros.push_back({ nullptr, nullptr });
    return macros;
}

class FileIncludeHandler final : public ID3DInclude {
public:
    explicit FileIncludeHandler(std::vector<std::filesystem::path> includeDirs)
        : m_includeDirs(std::move(includeDirs)) {}

    HRESULT STDMETHODCALLTYPE Open(
        D3D_INCLUDE_TYPE,
        LPCSTR fileName,
        LPCVOID,
        LPCVOID* data,
        UINT* bytes) override {
        if (!fileName || !data || !bytes) return E_INVALIDARG;
        *data = nullptr;
        *bytes = 0;

        const std::filesystem::path requested(fileName);
        std::vector<std::filesystem::path> candidates;
        if (requested.is_absolute()) {
            candidates.push_back(requested);
        } else {
            for (const auto& dir : m_includeDirs) {
                candidates.push_back(dir / requested);
            }
            candidates.push_back(requested);
        }

        for (const auto& candidate : candidates) {
            std::ifstream ifs(candidate, std::ios::binary | std::ios::ate);
            if (!ifs) continue;

            const std::streamsize size = ifs.tellg();
            if (size < 0) return E_FAIL;
            ifs.seekg(0, std::ios::beg);

            auto buffer = std::make_unique<char[]>(static_cast<size_t>(size));
            if (size > 0) {
                ifs.read(buffer.get(), size);
                if (!ifs) return E_FAIL;
            }

            *bytes = static_cast<UINT>(size);
            *data = buffer.release();
            return S_OK;
        }

        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override {
        delete[] static_cast<const char*>(data);
        return S_OK;
    }

private:
    std::vector<std::filesystem::path> m_includeDirs;
};

ShaderBytecode CompileD3DCompileInternal(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName,
    const D3D_SHADER_MACRO* macros,
    ID3DInclude* includeHandler) {

    if (hlslSource.empty()) throw std::runtime_error("D3DCompile: empty HLSL source");
    if (entryPoint.empty()) throw std::runtime_error("D3DCompile: empty entry point");
    if (target.empty()) throw std::runtime_error("D3DCompile: empty target");

    const std::string normalizedTarget = NormalizeD3DCompileTarget(target);
    const std::string sourceToCompile = WrapLegacyTexcoordPixelShaderForD3D12(
        hlslSource,
        normalizedTarget);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(
        sourceToCompile.data(),
        sourceToCompile.size(),
        sourceName.c_str(),
        macros,
        includeHandler,
        entryPoint.c_str(),
        normalizedTarget.c_str(),
        MakeCompileFlags(), 0,
        &blob, &errors);

    if (FAILED(hr)) {
        throw std::runtime_error(MakeCompileErrorMessage("D3DCompile failed", errors.Get()));
    }

    std::vector<uint8_t> data(blob->GetBufferSize());
    std::memcpy(data.data(), blob->GetBufferPointer(), data.size());
    return ShaderBytecode(std::move(data));
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring PathToWide(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.wstring();
#else
    return ToWide(path.string());
#endif
}

std::vector<std::wstring> BuildDxcExtraArgs(const ShaderCompileDesc& desc) {
    std::vector<std::wstring> args;

    for (const auto& dir : BuildIncludeDirs(desc)) {
        args.push_back(L"-I");
        args.push_back(PathToWide(dir));
    }

    for (const ShaderMacro& m : desc.defines) {
        if (m.name.empty()) {
            throw std::runtime_error("ShaderCompileDesc: define name must not be empty");
        }
        args.push_back(L"-D");
        args.push_back(ToWide(m.name + "=" + m.value));
    }

    return args;
}

} // unnamed namespace

ShaderBytecode LoadShaderBytecodeFromFile(const std::filesystem::path& csoPath) {
    std::ifstream ifs(csoPath, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::ostringstream oss;
        oss << "LoadShaderBytecodeFromFile: cannot open " << csoPath.string();
        throw std::runtime_error(oss.str());
    }
    const std::streamsize size = ifs.tellg();
    if (size <= 0) {
        throw std::runtime_error("LoadShaderBytecodeFromFile: empty file");
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!ifs) {
        throw std::runtime_error("LoadShaderBytecodeFromFile: read failed");
    }
    return ShaderBytecode(std::move(buffer));
}

ShaderBytecode CompileShaderFromFile(const ShaderCompileDesc& desc) {
    if (desc.sourcePath.empty()) {
        throw std::runtime_error("CompileShaderFromFile: empty sourcePath");
    }

    const std::string source = ReadTextFile(desc.sourcePath);
    return CompileShaderFromSource(source, desc, desc.sourcePath.string());
}

ShaderBytecode CompileShaderFromSource(
    const std::string& hlslSource,
    const ShaderCompileDesc& desc,
    const std::string& sourceName) {

    if (desc.entryPoint.empty()) throw std::runtime_error("ShaderCompileDesc: empty entryPoint");
    if (desc.target.empty()) throw std::runtime_error("ShaderCompileDesc: empty target");

    if (desc.useDxc) {
        return CompileShaderFromSource_Dxc(
            hlslSource, desc.entryPoint, desc.target, sourceName, BuildDxcExtraArgs(desc));
    }

    FileIncludeHandler includeHandler(BuildIncludeDirs(desc));
    std::vector<D3D_SHADER_MACRO> macros = MakeD3DCompileMacros(desc.defines);
    return CompileD3DCompileInternal(
        hlslSource,
        desc.entryPoint,
        desc.target,
        sourceName,
        macros.data(),
        &includeHandler);
}

// --------------------------------------------------------------------------
// D3DCompile (FXC) 経由
// --------------------------------------------------------------------------
ShaderBytecode CompileShaderFromSource_D3DCompile(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName) {

    return CompileD3DCompileInternal(
        hlslSource,
        entryPoint,
        target,
        sourceName,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE);
}

// --------------------------------------------------------------------------
// DXC 経由
// --------------------------------------------------------------------------
ShaderBytecode CompileShaderFromSource_Dxc(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& target,
    const std::string& sourceName,
    const std::vector<std::wstring>& extraArgs) {

    if (hlslSource.empty()) throw std::runtime_error("DXC: empty HLSL source");
    if (entryPoint.empty()) throw std::runtime_error("DXC: empty entry point");
    if (target.empty()) throw std::runtime_error("DXC: empty target");

    ComPtr<IDxcUtils>         utils;
    ComPtr<IDxcCompiler3>     compiler;
    ComPtr<IDxcIncludeHandler> includeHandler;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr)) {
        throw std::runtime_error("DXC: DxcCreateInstance(Utils) failed "
                                 "(is dxcompiler.dll available?)");
    }
    D3D12CORE_THROW_IF_FAILED(DxcCreateInstance(CLSID_DxcCompiler,
                                                IID_PPV_ARGS(&compiler)));
    D3D12CORE_THROW_IF_FAILED(utils->CreateDefaultIncludeHandler(&includeHandler));

    DxcBuffer src = {};
    src.Ptr      = hlslSource.data();
    src.Size     = hlslSource.size();
    src.Encoding = DXC_CP_UTF8;

    // 引数を組み立て
    const std::wstring wEntry    = ToWide(entryPoint);
    const std::wstring wTarget   = ToWide(target);
    const std::wstring wSrcName  = ToWide(sourceName);

    std::vector<LPCWSTR> args;
    args.push_back(wSrcName.c_str());
    args.push_back(L"-E"); args.push_back(wEntry.c_str());
    args.push_back(L"-T"); args.push_back(wTarget.c_str());
#ifdef _DEBUG
    args.push_back(L"-Zi");
    args.push_back(L"-Od");
#else
    args.push_back(L"-O3");
#endif
    for (const auto& a : extraArgs) args.push_back(a.c_str());

    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&src, args.data(), static_cast<UINT32>(args.size()),
                           includeHandler.Get(), IID_PPV_ARGS(&result));
    if (FAILED(hr)) {
        throw std::runtime_error("DXC: Compile() call failed");
    }

    // エラー / 警告メッセージ
    ComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))
        && errors && errors->GetStringLength() > 0) {
        HRESULT status = S_OK;
        result->GetStatus(&status);
        if (FAILED(status)) {
            throw std::runtime_error(std::string("DXC compile failed: ")
                                     + errors->GetStringPointer());
        }
        // 警告はスルー（必要なら OutputDebugString に出してもよい）
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        throw std::runtime_error("DXC compile failed (no error message)");
    }

    ComPtr<IDxcBlob> blob;
    D3D12CORE_THROW_IF_FAILED(result->GetOutput(DXC_OUT_OBJECT,
                                                IID_PPV_ARGS(&blob), nullptr));
    if (!blob || blob->GetBufferSize() == 0) {
        throw std::runtime_error("DXC: no DXIL output");
    }

    std::vector<uint8_t> data(blob->GetBufferSize());
    std::memcpy(data.data(), blob->GetBufferPointer(), data.size());
    return ShaderBytecode(std::move(data));
}

} // namespace D3D12CoreLib
