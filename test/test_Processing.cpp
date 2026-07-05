#include "TestCommon.hpp"
#include "D3D12Processing/D3D12Processing.hpp"

#include <filesystem>
#include <memory>
#include <string>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "ConvertRgbToRgb.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    // CTest sets the working directory to the test executable directory and
    // test/CMakeLists.txt copies ../shaders there.  Prefer this runtime path
    // because std::filesystem::current_path() is obtained from the OS as a
    // native path and remains valid even when the repository path contains
    // non-ASCII characters.
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(runtimeDir)) {
        return runtimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    // Fallback for direct execution outside CTest.  CMake passes this macro as
    // UTF-8, so construct the filesystem path with u8path on Windows/MSVC
    // instead of the locale-dependent narrow path constructor.
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasProcessingShader(sourceDir)) {
        return sourceDir;
    }
#endif

    return runtimeDir;
}

void RequireProcessingShader(D3D12ProcessingShaderCache& cache, const char* fileName) {
    try {
        const auto& bytecode = cache.GetComputeShader(fileName);
        CHECK(!bytecode.Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile processing shader ") + fileName + ": " + e.what());
    }
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

} // namespace

TEST(Processing, TypesAndRectValidation) {
    CHECK(IsRgbaLikeFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
    CHECK(IsRgbaLikeFormat(DXGI_FORMAT_B8G8R8A8_UNORM));
    CHECK(IsSupportedProcessingFormat(DXGI_FORMAT_NV12));
    CHECK(!IsSupportedProcessingFormat(DXGI_FORMAT_R32_FLOAT));

    auto r = ResolveRect({}, 640, 480);
    CHECK_EQ(r.width, 640u);
    CHECK_EQ(r.height, 480u);

    CHECK_THROWS(ValidateRectInside(ProcessingRect{ 10, 0, 100, 10 }, 32, 32, "test", "rect"));
    CHECK_THROWS(ValidateEvenSize(5, 4, DXGI_FORMAT_NV12, "test"));
}

TEST(Processing, ContextInitializesAndQueriesCaps) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    CHECK(fx.context.GetDevice() == core->GetDevice());
    CHECK(fx.context.Caps().rgba8Uav || !fx.context.Caps().rgba8Uav); // query completed without throwing
}

TEST(Processing, ShaderCacheCompilesProcessingShaders) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    RequireProcessingShader(cache, "ConvertRgbToRgb.hlsl");
    RequireProcessingShader(cache, "ConvertNv12ToRgb.hlsl");
    RequireProcessingShader(cache, "ConvertRgbToNv12.hlsl");
    RequireProcessingShader(cache, "ResizeRgba.hlsl");
}

TEST(Processing, RgbaViewSetCreatesSrvAndUav) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    auto tex = CreateTexture2D(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto views = CreateRgbaTextureViewSet(fx.context, tex, true, true);
    CHECK(views.range.IsValid());
    CHECK(views.HasSrv());
    CHECK(views.HasUav());
    CHECK_EQ(views.range.count, 2u);
}

TEST(Processing, Nv12ViewValidation) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    auto rgba = CreateTexture2D(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    CHECK_THROWS(CreateNv12SrvViewSet(fx.context, rgba));

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    CHECK_THROWS(converter.CreateOutputTexture(*core, 15, 16, DXGI_FORMAT_NV12));
    CHECK_THROWS(converter.CreateOutputTexture(*core, 16, 16, DXGI_FORMAT_R32_FLOAT));
}

TEST(Processing, ConverterAndResizerCreateOutputs) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto out = converter.CreateOutputTexture(*core, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(out.Get() != nullptr);
    CHECK_EQ(out.GetFormat(), DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D12Resizer resizer;
    resizer.Initialize(fx.context);
    auto resized = resizer.CreateOutputTexture(*core, 8, 8, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(resized.Get() != nullptr);
    CHECK_EQ(resized.GetWidth(), 8ull);
    CHECK_THROWS(resizer.CreateOutputTexture(*core, 8, 8, DXGI_FORMAT_NV12));
}
