#include "TestCommon.hpp"
#include "D3D12Processing/D3D12Processing.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true);
        sampler.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDir());
    }
};

struct TextureReadback {
    D3D12ReadbackBuffer buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    UINT width = 0;
    UINT height = 0;
};

TextureReadback RecordReadbackTexture2D(
    D3D12Core& core,
    D3D12CommandContext& commandContext,
    D3D12Resource& texture) {

    if (!texture.Get()) {
        TEST_FAIL("RecordReadbackTexture2D: null texture");
    }

    const auto desc = texture.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        TEST_FAIL("RecordReadbackTexture2D: resource is not Texture2D");
    }
    if (desc.DepthOrArraySize != 1 || desc.MipLevels != 1) {
        TEST_FAIL("RecordReadbackTexture2D: only single mip / single array textures are supported by this test helper");
    }

    TextureReadback rb;
    rb.width = static_cast<UINT>(desc.Width);
    rb.height = desc.Height;
    core.GetDevice()->GetCopyableFootprints(
        &desc,
        0,
        1,
        0,
        &rb.layout,
        &rb.numRows,
        &rb.rowSize,
        &rb.totalBytes);

    rb.buffer.Initialize(core.GetDevice(), rb.totalBytes);

    const auto before = texture.GetState();
    if (before != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        commandContext.ResourceBarrier(MakeTransitionBarrier(
            texture.Get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE));
        texture.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = rb.buffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = rb.layout;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    commandContext.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    return rb;
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

std::vector<uint8_t> ReadbackCompactRgbaLike(TextureReadback& rb) {
    const UINT bytesPerRow = rb.width * 4u;
    if (rb.rowSize < bytesPerRow) {
        TEST_FAIL("ReadbackCompactRgbaLike: rowSize is smaller than width * 4");
    }
    if (rb.numRows < rb.height) {
        TEST_FAIL("ReadbackCompactRgbaLike: numRows is smaller than texture height");
    }

    std::vector<uint8_t> out(static_cast<size_t>(bytesPerRow) * rb.height);
    const auto* mapped = static_cast<const uint8_t*>(rb.buffer.Map());
    const auto* base = mapped + rb.layout.Offset;
    for (UINT y = 0; y < rb.height; ++y) {
        std::memcpy(
            out.data() + static_cast<size_t>(y) * bytesPerRow,
            base + static_cast<size_t>(y) * rb.layout.Footprint.RowPitch,
            bytesPerRow);
    }
    rb.buffer.Unmap();
    return out;
}

void CheckBytesEqual(
    const std::vector<uint8_t>& actual,
    const std::vector<uint8_t>& expected,
    const char* label) {

    if (actual.size() != expected.size()) {
        std::ostringstream os;
        os << label << ": size mismatch actual=" << actual.size()
           << " expected=" << expected.size();
        TEST_FAIL(os.str());
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::ostringstream os;
            os << label << ": byte mismatch at index " << i
               << " actual=" << static_cast<int>(actual[i])
               << " expected=" << static_cast<int>(expected[i]);
            TEST_FAIL(os.str());
        }
    }
}

std::vector<uint8_t> MakePointResizeExpected2x2To4x4(const std::vector<uint8_t>& src) {
    std::vector<uint8_t> expected(4u * 4u * 4u);
    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const UINT sx = x / 2u;
            const UINT sy = y / 2u;
            const size_t srcIndex = static_cast<size_t>(sy * 2u + sx) * 4u;
            const size_t dstIndex = static_cast<size_t>(y * 4u + x) * 4u;
            std::memcpy(expected.data() + dstIndex, src.data() + srcIndex, 4u);
        }
    }
    return expected;
}

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

TEST(Processing, FormatConverterRgbaCopyReadbackMatchesPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const UINT width = 4;
    const UINT height = 4;
    std::vector<uint8_t> srcPixels(static_cast<size_t>(width) * height * 4u);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            srcPixels[i + 0] = static_cast<uint8_t>(10u + x * 20u);
            srcPixels[i + 1] = static_cast<uint8_t>(30u + y * 30u);
            srcPixels[i + 2] = static_cast<uint8_t>(50u + x * 7u + y * 11u);
            srcPixels[i + 3] = static_cast<uint8_t>(255u - x * 3u - y * 5u);
        }
    }

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(
        *core,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    FormatConvertDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    converter.RecordConvert(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    auto actual = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(actual, srcPixels, "RGBA -> RGBA processing readback");
}

TEST(Processing, FormatConverterRgbaToBgraReadbackSwizzlesPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsBgra8Uav()) {
        TEST_SKIP("B8G8R8A8 UAV typed store is not supported");
    }

    const UINT width = 2;
    const UINT height = 2;
    const std::vector<uint8_t> srcPixels = {
        10,  20,  30, 255,
        40,  50,  60, 240,
        70,  80,  90, 230,
        100, 110, 120, 220,
    };
    const std::vector<uint8_t> expectedBgraBytes = {
        30,  20,  10, 255,
        60,  50,  40, 240,
        90,  80,  70, 230,
        120, 110, 100, 220,
    };

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12FormatConverter converter;
    converter.Initialize(fx.context);
    auto dst = converter.CreateOutputTexture(
        *core,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    FormatConvertDesc desc = {};
    desc.srcFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.dstFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    converter.RecordConvert(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    auto actual = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(actual, expectedBgraBytes, "RGBA -> BGRA processing readback");
}

TEST(Processing, ResizerPointReadbackMatchesNearestPixels) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcPixels = {
        255,   0,   0, 255,   0, 255,   0, 255,
          0,   0, 255, 255, 255, 255,   0, 255,
    };
    const auto expected = MakePointResizeExpected2x2To4x4(srcPixels);

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        2,
        2,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12Resizer resizer;
    resizer.Initialize(fx.context);
    auto dst = resizer.CreateOutputTexture(
        *core,
        4,
        4,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON);

    ResizeDesc desc = {};
    desc.filter = ProcessingFilter::Point;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    resizer.RecordResize(commandContext, src, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    auto actual = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(actual, expected, "RGBA point resize 2x2 -> 4x4 readback");
}
