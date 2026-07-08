#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

bool HasAdvancedProcessingShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "AdvancedTransformRgba.hlsl", ec) && !ec &&
           std::filesystem::exists(dir / "ApplyLut3D.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasAdvancedProcessingShader(runtimeDir)) {
        return runtimeDir;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasAdvancedProcessingShader(sourceDir)) {
        return sourceDir;
    }
#endif

    return runtimeDir;
}

struct AdvancedProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit AdvancedProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

void RequireProcessingShader(D3D12ProcessingShaderCache& cache, const char* fileName) {
    try {
        const auto& bytecode = cache.GetComputeShader(fileName);
        CHECK(!bytecode.Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile processing shader ") + fileName + ": " + e.what());
    }
}

} // namespace

TEST(AdvancedProcessing, ShaderCacheCompilesAdvancedShaders) {
    REQUIRE_CORE(core);
    AdvancedProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    RequireProcessingShader(cache, "AdvancedTransformRgba.hlsl");
    RequireProcessingShader(cache, "ApplyLut3D.hlsl");
}

TEST(AdvancedProcessing, PublicTypesHaveExpectedDefaults) {
    AffineTransformDesc affine = {};
    CHECK(affine.dstToSrc[0] == 1.0f);
    CHECK(affine.dstToSrc[4] == 1.0f);
    CHECK(affine.filter == ProcessingFilter::Linear);

    PerspectiveTransformDesc perspective = {};
    CHECK(perspective.dstToSrc[0] == 1.0f);
    CHECK(perspective.dstToSrc[4] == 1.0f);
    CHECK(perspective.dstToSrc[8] == 1.0f);

    Lut3DDesc lut = {};
    CHECK(lut.strength == 1.0f);
    CHECK(lut.preserveAlpha);
}

TEST(AdvancedProcessing, ProcessorInitializesAndCreatesOutputTexture) {
    REQUIRE_CORE(core);
    AdvancedProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);

    auto output = processor.CreateOutputTexture(*core, 16, 8, DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(output.Get() != nullptr);
    const auto desc = output.GetDesc();
    CHECK(desc.Width == 16);
    CHECK(desc.Height == 8);
    CHECK((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
}
