#include "TestCommon.hpp"
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

struct TextureReadback {
    D3D12ReadbackBuffer buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    UINT width = 0;
    UINT height = 0;
};

D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

void RequireProcessingShader(D3D12ProcessingShaderCache& cache, const char* fileName) {
    try {
        const auto& bytecode = cache.GetComputeShader(fileName);
        CHECK(!bytecode.Empty());
    } catch (const std::exception& e) {
        TEST_FAIL(std::string("failed to compile processing shader ") + fileName + ": " + e.what());
    }
}

void ExecuteAndWait(D3D12Core& core, D3D12CommandContext& commandContext) {
    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    core.DirectQueue().WaitForFenceValue(core.DirectQueue().Signal());
}

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

std::vector<uint8_t> ReadbackCompactRgba8(TextureReadback& rb) {
    const UINT bytesPerRow = rb.width * 4u;
    if (rb.rowSize < bytesPerRow) {
        TEST_FAIL("ReadbackCompactRgba8: rowSize is smaller than width * 4");
    }
    if (rb.numRows < rb.height) {
        TEST_FAIL("ReadbackCompactRgba8: numRows is smaller than texture height");
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

D3D12Resource CreateTexture3DFromRgba8(
    D3D12Core& core,
    UINT width,
    UINT height,
    UINT depth,
    const std::vector<uint8_t>& rgba,
    D3D12_RESOURCE_STATES finalState) {

    if (width == 0 || height == 0 || depth == 0) {
        TEST_FAIL("CreateTexture3DFromRgba8: zero dimension");
    }
    if (rgba.size() != static_cast<size_t>(width) * height * depth * 4u) {
        TEST_FAIL("CreateTexture3DFromRgba8: source size mismatch");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    const auto defaultHeap = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> resource;
    D3D12CORE_THROW_IF_FAILED(core.GetDevice()->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource)));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    core.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSize, &totalBytes);

    D3D12UploadBuffer upload;
    upload.Initialize(core.GetDevice(), totalBytes);

    auto* dstBase = static_cast<uint8_t*>(upload.Map()) + layout.Offset;
    const UINT64 dstRowPitch = layout.Footprint.RowPitch;
    const UINT64 dstSlicePitch = dstRowPitch * numRows;
    const UINT64 srcRowPitch = static_cast<UINT64>(width) * 4u;
    const UINT64 srcSlicePitch = srcRowPitch * height;
    for (UINT z = 0; z < depth; ++z) {
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(
                dstBase + static_cast<size_t>(z * dstSlicePitch + y * dstRowPitch),
                rgba.data() + static_cast<size_t>(z * srcSlicePitch + y * srcRowPitch),
                static_cast<size_t>(srcRowPitch));
        }
    }

    D3D12Resource texture(std::move(resource), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12CommandContext ctx = core.CreateDirectContext();
    ctx.Reset();

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = upload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = layout;

    ctx.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    if (finalState != D3D12_RESOURCE_STATE_COPY_DEST) {
        ctx.ResourceBarrier(MakeTransitionBarrier(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState));
        texture.SetState(finalState);
    }
    ExecuteAndWait(core, ctx);
    return texture;
}

std::vector<uint8_t> MakeIdentity2x2x2LutRgba8() {
    std::vector<uint8_t> lut(2u * 2u * 2u * 4u);
    for (UINT z = 0; z < 2; ++z) {
        for (UINT y = 0; y < 2; ++y) {
            for (UINT x = 0; x < 2; ++x) {
                const size_t i = static_cast<size_t>((z * 4u + y * 2u + x) * 4u);
                lut[i + 0] = x ? 255 : 0;
                lut[i + 1] = y ? 255 : 0;
                lut[i + 2] = z ? 255 : 0;
                lut[i + 3] = 255;
            }
        }
    }
    return lut;
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

TEST(AdvancedProcessing, AffineIdentityMatchesSource) {
    REQUIRE_CORE(core);
    AdvancedProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcBytes = {
        255, 0,   0,   255,   0,   255, 0,   255,
        0,   0,   255, 255,   255, 255, 0,   255,
    };
    auto src = CreateTexture2DFromMemory(
        *core, srcBytes.data(), 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 2 * 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    AffineTransformDesc desc = {};
    desc.filter = ProcessingFilter::Point;
    desc.dstRect = ProcessingRect{ 0, 0, 2, 2 };

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    processor.RecordAffineTransform(ctx, src, dst, desc);
    auto rb = RecordReadbackTexture2D(*core, ctx, dst);
    ExecuteAndWait(*core, ctx);

    const auto actual = ReadbackCompactRgba8(rb);
    CheckBytesEqual(actual, srcBytes, "AffineIdentityMatchesSource");
}

TEST(AdvancedProcessing, PerspectiveIdentityMatchesSource) {
    REQUIRE_CORE(core);
    AdvancedProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcBytes = {
        10,  20,  30,  255,   40,  50,  60,  255,
        70,  80,  90,  255,   100, 110, 120, 255,
    };
    auto src = CreateTexture2DFromMemory(
        *core, srcBytes.data(), 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 2 * 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    PerspectiveTransformDesc desc = {};
    desc.filter = ProcessingFilter::Point;
    desc.dstRect = ProcessingRect{ 0, 0, 2, 2 };

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    processor.RecordPerspectiveTransform(ctx, src, dst, desc);
    auto rb = RecordReadbackTexture2D(*core, ctx, dst);
    ExecuteAndWait(*core, ctx);

    const auto actual = ReadbackCompactRgba8(rb);
    CheckBytesEqual(actual, srcBytes, "PerspectiveIdentityMatchesSource");
}

TEST(AdvancedProcessing, Lut3DIdentityPreservesColorAndAlpha) {
    REQUIRE_CORE(core);
    AdvancedProcessingFixture fx(core);
    if (!fx.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const std::vector<uint8_t> srcBytes = { 255, 0, 0, 128 };
    auto src = CreateTexture2DFromMemory(
        *core, srcBytes.data(), 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    auto lut = CreateTexture3DFromRgba8(
        *core, 2, 2, 2, MakeIdentity2x2x2LutRgba8(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12AdvancedProcessor processor;
    processor.Initialize(fx.context);
    auto dst = processor.CreateOutputTexture(*core, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);

    Lut3DDesc desc = {};
    desc.dstRect = ProcessingRect{ 0, 0, 1, 1 };
    desc.strength = 1.0f;
    desc.preserveAlpha = true;

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    processor.RecordApplyLut3D(ctx, src, lut, dst, desc);
    auto rb = RecordReadbackTexture2D(*core, ctx, dst);
    ExecuteAndWait(*core, ctx);

    const auto actual = ReadbackCompactRgba8(rb);
    CheckBytesEqual(actual, srcBytes, "Lut3DIdentityPreservesColorAndAlpha");
}
