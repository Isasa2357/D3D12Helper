#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

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

bool HasRegionBlurShader(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "RegionBlurBlendRgba.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDir() {
    const auto runtimeDir = std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasRegionBlurShader(runtimeDir)) {
        return runtimeDir;
    }
#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto sourceDir = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasRegionBlurShader(sourceDir)) {
        return sourceDir;
    }
#endif
    return runtimeDir;
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> c) : core(std::move(c)) {
        cbvSrvUav.Initialize(core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, true);
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

TextureReadback RecordReadbackTexture2D(D3D12Core& core, D3D12CommandContext& commandContext, D3D12Resource& texture) {
    const auto desc = texture.GetDesc();

    TextureReadback rb;
    rb.width = static_cast<UINT>(desc.Width);
    rb.height = desc.Height;
    core.GetDevice()->GetCopyableFootprints(
        &desc, 0, 1, 0,
        &rb.layout,
        &rb.numRows,
        &rb.rowSize,
        &rb.totalBytes);

    rb.buffer.Initialize(core.GetDevice(), rb.totalBytes);

    const auto before = texture.GetState();
    if (before != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        commandContext.ResourceBarrier(MakeTransitionBarrier(texture.Get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE));
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

void CheckBytesEqual(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, const char* label) {
    if (actual.size() != expected.size()) {
        std::ostringstream os;
        os << label << ": size mismatch actual=" << actual.size() << " expected=" << expected.size();
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

} // namespace

TEST(ProcessingRegionBlur, ShaderCompile) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fx.context);
    CHECK(!cache.GetComputeShader("RegionBlurBlendRgba.hlsl").Empty());
}

TEST(ProcessingRegionBlur, RadiusZeroReadbackCopiesInput) {
    REQUIRE_CORE(core);
    ProcessingFixture fx(core);

    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    constexpr UINT width = 4;
    constexpr UINT height = 4;
    std::vector<uint8_t> srcPixels(static_cast<size_t>(width) * height * 4u);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y * width + x) * 4u;
            srcPixels[i + 0] = static_cast<uint8_t>(10u + x * 20u);
            srcPixels[i + 1] = static_cast<uint8_t>(30u + y * 30u);
            srcPixels[i + 2] = static_cast<uint8_t>(50u + x * 7u + y * 11u);
            srcPixels[i + 3] = 255;
        }
    }

    auto src = CreateTexture2DFromRGBA(
        *core,
        srcPixels.data(),
        width,
        height,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12RegionBlur processor;
    processor.Initialize(fx.context);
    auto scratch = processor.CreateScratchTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto blurred = processor.CreateBlurredTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto dst = processor.CreateOutputTexture(*core, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    RegionBlurDesc desc = {};
    desc.shape = RegionShape::Circle;
    desc.selection = RegionSelection::Outside;
    desc.centerX = 2.0f;
    desc.centerY = 2.0f;
    desc.radius = 1.0f;
    desc.edgeSoftness = 0.0f;
    desc.blurStrength = 1.0f;
    desc.blurMode = BlurMode::Gaussian;
    desc.blurRadius = 0;
    desc.blurSigma = 1.0f;

    auto commandContext = core->CreateDirectContext();
    commandContext.Reset();
    processor.RecordRegionBlur(commandContext, src, scratch, blurred, dst, desc);
    auto readback = RecordReadbackTexture2D(*core, commandContext, dst);
    ExecuteAndWait(*core, commandContext);

    const auto got = ReadbackCompactRgbaLike(readback);
    CheckBytesEqual(got, srcPixels, "region blur radius zero");
}
