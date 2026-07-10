//
// test_YuvHlslPrimitives.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Framework.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

float Clamp(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

float Clamp01(float value) {
    return Clamp(value, 0.0f, 1.0f);
}

float RoundCode(float value) {
    return std::floor(value + 0.5f);
}

float CodeMaximum(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_P010 ? 1023.0f : 255.0f;
}

float EightBitScale(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_P010 ? 4.0f : 1.0f;
}

float ChromaCenter(DXGI_FORMAT format) {
    return 128.0f * EightBitScale(format);
}

float CodeToSample(float code, DXGI_FORMAT format) {
    code = Clamp(RoundCode(code), 0.0f, CodeMaximum(format));
    return format == DXGI_FORMAT_P010
        ? code * 64.0f / 65535.0f
        : code / 255.0f;
}

float SampleToCode(float sample, DXGI_FORMAT format) {
    sample = Clamp01(sample);
    if (format == DXGI_FORMAT_P010) {
        return Clamp(RoundCode(sample * 65535.0f / 64.0f), 0.0f, 1023.0f);
    }
    return Clamp(RoundCode(sample * 255.0f), 0.0f, 255.0f);
}

Float3 RgbToYuvSignal(Float3 rgb, ProcessingColorMatrix matrix) {
    rgb.x = Clamp01(rgb.x);
    rgb.y = Clamp01(rgb.y);
    rgb.z = Clamp01(rgb.z);

    Float3 result;
    if (matrix == ProcessingColorMatrix::BT601) {
        result.x = 0.299000f * rgb.x + 0.587000f * rgb.y + 0.114000f * rgb.z;
        result.y = (rgb.z - result.x) / 1.772000f;
        result.z = (rgb.x - result.x) / 1.402000f;
    } else if (matrix == ProcessingColorMatrix::BT2020) {
        result.x = 0.262700f * rgb.x + 0.678000f * rgb.y + 0.059300f * rgb.z;
        result.y = (rgb.z - result.x) / 1.881400f;
        result.z = (rgb.x - result.x) / 1.474600f;
    } else {
        result.x = 0.212600f * rgb.x + 0.715200f * rgb.y + 0.072200f * rgb.z;
        result.y = (rgb.z - result.x) / 1.855600f;
        result.z = (rgb.x - result.x) / 1.574800f;
    }
    return result;
}

Float3 YuvToRgbSignal(Float3 signal, ProcessingColorMatrix matrix) {
    Float3 result;
    if (matrix == ProcessingColorMatrix::BT601) {
        result.x = signal.x + 1.402000f * signal.z;
        result.y = signal.x - 0.344136f * signal.y - 0.714136f * signal.z;
        result.z = signal.x + 1.772000f * signal.y;
    } else if (matrix == ProcessingColorMatrix::BT2020) {
        result.x = signal.x + 1.474600f * signal.z;
        result.y = signal.x - 0.164553f * signal.y - 0.571353f * signal.z;
        result.z = signal.x + 1.881400f * signal.y;
    } else {
        result.x = signal.x + 1.574800f * signal.z;
        result.y = signal.x - 0.187324f * signal.y - 0.468124f * signal.z;
        result.z = signal.x + 1.855600f * signal.y;
    }
    result.x = Clamp01(result.x);
    result.y = Clamp01(result.y);
    result.z = Clamp01(result.z);
    return result;
}

Float3 EncodeCode(
    Float3 rgb,
    DXGI_FORMAT format,
    ProcessingColorRange range,
    ProcessingColorMatrix matrix) {

    const Float3 signal = RgbToYuvSignal(rgb, matrix);
    const float scale = EightBitScale(format);
    const float maximum = CodeMaximum(format);

    Float3 code;
    if (range == ProcessingColorRange::Limited) {
        code.x = Clamp(16.0f * scale + 219.0f * scale * signal.x,
            16.0f * scale, 235.0f * scale);
        code.y = Clamp(128.0f * scale + 224.0f * scale * signal.y,
            16.0f * scale, 240.0f * scale);
        code.z = Clamp(128.0f * scale + 224.0f * scale * signal.z,
            16.0f * scale, 240.0f * scale);
    } else {
        code.x = Clamp(maximum * signal.x, 0.0f, maximum);
        code.y = Clamp(ChromaCenter(format) + maximum * signal.y, 0.0f, maximum);
        code.z = Clamp(ChromaCenter(format) + maximum * signal.z, 0.0f, maximum);
    }

    code.x = RoundCode(code.x);
    code.y = RoundCode(code.y);
    code.z = RoundCode(code.z);
    return code;
}

Float3 DecodeCode(
    Float3 code,
    DXGI_FORMAT format,
    ProcessingColorRange range,
    ProcessingColorMatrix matrix) {

    const float scale = EightBitScale(format);
    const float maximum = CodeMaximum(format);

    Float3 signal;
    if (range == ProcessingColorRange::Limited) {
        signal.x = (code.x - 16.0f * scale) / (219.0f * scale);
        signal.y = (code.y - 128.0f * scale) / (224.0f * scale);
        signal.z = (code.z - 128.0f * scale) / (224.0f * scale);
    } else {
        signal.x = code.x / maximum;
        signal.y = (code.y - ChromaCenter(format)) / maximum;
        signal.z = (code.z - ChromaCenter(format)) / maximum;
    }
    return YuvToRgbSignal(signal, matrix);
}

uint8_t ToByte(float value) {
    return static_cast<uint8_t>(Clamp(RoundCode(Clamp01(value) * 255.0f), 0.0f, 255.0f));
}

void CheckCode(Float3 actual, Float3 expected, const char* label) {
    if (actual.x != expected.x || actual.y != expected.y || actual.z != expected.z) {
        std::ostringstream os;
        os << label << ": actual=(" << actual.x << ", " << actual.y << ", " << actual.z
           << ") expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ")";
        TEST_FAIL(os.str());
    }
}

void CheckRgbNear(Float3 actual, Float3 expected, float epsilon, const char* label) {
    if (std::fabs(actual.x - expected.x) > epsilon ||
        std::fabs(actual.y - expected.y) > epsilon ||
        std::fabs(actual.z - expected.z) > epsilon) {
        std::ostringstream os;
        os << label << ": actual=(" << actual.x << ", " << actual.y << ", " << actual.z
           << ") expected=(" << expected.x << ", " << expected.y << ", " << expected.z
           << ") epsilon=" << epsilon;
        TEST_FAIL(os.str());
    }
}

bool HasPrimitiveShader(const std::filesystem::path& directory) {
    std::error_code ec;
    return std::filesystem::exists(directory / "YuvPrimitives.hlsli", ec) && !ec &&
           std::filesystem::exists(directory / "YuvPrimitiveProbe.hlsl", ec) && !ec;
}

std::filesystem::path ProcessingShaderDirectory() {
    const auto runtime =
        std::filesystem::current_path() / "shaders" / "D3D12Processing";
    if (HasPrimitiveShader(runtime)) {
        return runtime;
    }

#ifdef D3D12HELPER_TEST_SOURCE_DIR
    const auto source = std::filesystem::u8path(D3D12HELPER_TEST_SOURCE_DIR)
        .parent_path() / "shaders" / "D3D12Processing";
    if (HasPrimitiveShader(source)) {
        return source;
    }
#endif

    return runtime;
}

struct ProcessingFixture {
    std::shared_ptr<D3D12Core> core;
    D3D12DescriptorAllocator cbvSrvUav;
    D3D12DescriptorAllocator sampler;
    D3D12ProcessingContext context;

    explicit ProcessingFixture(std::shared_ptr<D3D12Core> value)
        : core(std::move(value)) {
        cbvSrvUav.Initialize(
            core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true);
        sampler.Initialize(
            core->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, true);
        context.Initialize(*core, &cbvSrvUav, &sampler, ProcessingShaderDirectory());
    }
};

std::vector<uint8_t> ExecuteProbe(ProcessingFixture& fixture) {
    D3D12ProcessingShaderCache cache;
    cache.Initialize(fixture.context);
    const auto& shader = cache.GetComputeShader("YuvPrimitiveProbe.hlsl");

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.numUavs = 1;

    D3D12ComputePipeline pipeline;
    pipeline.InitializeWithTemplate(fixture.context.GetDevice(), shader, pipelineDesc);

    auto output = CreateTexture2D(
        *fixture.core,
        8,
        1,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto views = CreateRgbaTextureViewSet(
        fixture.context,
        output,
        false,
        true,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    const auto outputDesc = output.GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    fixture.core->GetDevice()->GetCopyableFootprints(
        &outputDesc, 0, 1, 0, &layout, &rows, &rowSize, &totalBytes);

    D3D12ReadbackBuffer readback;
    readback.Initialize(fixture.core->GetDevice(), totalBytes);

    auto commandContext = fixture.core->CreateDirectContext();
    commandContext.Reset();
    commandContext.ResourceBarrier(MakeTransitionBarrier(
        output.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    ID3D12DescriptorHeap* heaps[] = {
        fixture.context.CbvSrvUavAllocator().GetHeap()
    };
    commandContext.GetCommandList()->SetDescriptorHeaps(1, heaps);
    pipeline.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        pipeline.UavTableIndex(), views.Gpu(views.uavIndex));
    pipeline.Dispatch(commandContext, 1, 1, 1);

    commandContext.ResourceBarrier(MakeUavBarrier(output.Get()));
    commandContext.ResourceBarrier(MakeTransitionBarrier(
        output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = output.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    commandContext.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    commandContext.Close();
    ID3D12CommandList* lists[] = { commandContext.GetCommandList() };
    fixture.core->DirectQueue().ExecuteCommandLists(1, lists);
    fixture.core->DirectQueue().WaitForFenceValue(fixture.core->DirectQueue().Signal());

    std::vector<uint8_t> bytes(8u * 4u);
    auto mapped = readback.MapRead(layout.Offset, rowSize);
    std::memcpy(bytes.data(), mapped.Data(), bytes.size());
    return bytes;
}

std::array<Float3, 8> ProbeCodes() {
    return {{
        { 81.0f, 90.0f, 240.0f },
        { 63.0f, 102.0f, 240.0f },
        { 74.0f, 97.0f, 240.0f },
        { 16.0f, 128.0f, 128.0f },
        { 235.0f, 128.0f, 128.0f },
        { 250.0f, 409.0f, 960.0f },
        { 54.0f, 99.0f, 255.0f },
        { 217.0f, 395.0f, 1023.0f },
    }};
}

std::array<Float3, 8> ExpectedProbeRgb() {
    const auto code = ProbeCodes();
    return {{
        DecodeCode(code[0], DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT601),
        DecodeCode(code[1], DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        DecodeCode(code[2], DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT2020),
        DecodeCode(code[3], DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        DecodeCode(code[4], DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        DecodeCode(code[5], DXGI_FORMAT_P010, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        DecodeCode(code[6], DXGI_FORMAT_NV12, ProcessingColorRange::Full, ProcessingColorMatrix::BT709),
        DecodeCode(code[7], DXGI_FORMAT_P010, ProcessingColorRange::Full, ProcessingColorMatrix::BT709),
    }};
}

} // namespace

TEST(YuvHlslPrimitives, KnownLimitedRangeRedCodesMatchStandards) {
    const Float3 red = { 1.0f, 0.0f, 0.0f };

    CheckCode(
        EncodeCode(red, DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT601),
        { 81.0f, 90.0f, 240.0f },
        "BT.601 NV12 limited red");
    CheckCode(
        EncodeCode(red, DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        { 63.0f, 102.0f, 240.0f },
        "BT.709 NV12 limited red");
    CheckCode(
        EncodeCode(red, DXGI_FORMAT_NV12, ProcessingColorRange::Limited, ProcessingColorMatrix::BT2020),
        { 74.0f, 97.0f, 240.0f },
        "BT.2020 NV12 limited red");

    CheckCode(
        EncodeCode(red, DXGI_FORMAT_P010, ProcessingColorRange::Limited, ProcessingColorMatrix::BT601),
        { 326.0f, 361.0f, 960.0f },
        "BT.601 P010 limited red");
    CheckCode(
        EncodeCode(red, DXGI_FORMAT_P010, ProcessingColorRange::Limited, ProcessingColorMatrix::BT709),
        { 250.0f, 409.0f, 960.0f },
        "BT.709 P010 limited red");
    CheckCode(
        EncodeCode(red, DXGI_FORMAT_P010, ProcessingColorRange::Limited, ProcessingColorMatrix::BT2020),
        { 294.0f, 387.0f, 960.0f },
        "BT.2020 P010 limited red");
}

TEST(YuvHlslPrimitives, P010StorageRoundTripsTenBitCodes) {
    const std::array<float, 8> codes = {
        0.0f, 1.0f, 64.0f, 250.0f, 512.0f, 940.0f, 960.0f, 1023.0f
    };
    for (float code : codes) {
        CHECK(SampleToCode(CodeToSample(code, DXGI_FORMAT_P010), DXGI_FORMAT_P010) == code);
    }
}

TEST(YuvHlslPrimitives, CpuReferenceRoundTripsMatricesRangesAndFormats) {
    const std::array<Float3, 5> colors = {{
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.1f, 0.4f, 0.8f },
    }};
    const std::array<ProcessingColorMatrix, 3> matrices = {
        ProcessingColorMatrix::BT601,
        ProcessingColorMatrix::BT709,
        ProcessingColorMatrix::BT2020,
    };
    const std::array<ProcessingColorRange, 2> ranges = {
        ProcessingColorRange::Full,
        ProcessingColorRange::Limited,
    };
    const std::array<DXGI_FORMAT, 2> formats = {
        DXGI_FORMAT_NV12,
        DXGI_FORMAT_P010,
    };

    for (DXGI_FORMAT format : formats) {
        const float epsilon = format == DXGI_FORMAT_P010 ? 0.0040f : 0.0120f;
        for (ProcessingColorRange range : ranges) {
            for (ProcessingColorMatrix matrix : matrices) {
                for (const auto& color : colors) {
                    const auto decoded = DecodeCode(EncodeCode(color, format, range, matrix), format, range, matrix);
                    CheckRgbNear(decoded, color, epsilon, "CPU YUV round trip");
                }
            }
        }
    }
}

TEST(YuvHlslPrimitives, RefactoredShadersCompileWithPrimitiveLibrary) {
    REQUIRE_CORE(core);
    ProcessingFixture fixture(core);

    D3D12ProcessingShaderCache cache;
    cache.Initialize(fixture.context);

    CHECK(!cache.GetComputeShader("YuvPrimitiveProbe.hlsl").Empty());
    CHECK(!cache.GetComputeShader("ConvertNv12ToRgb.hlsl").Empty());
    CHECK(!cache.GetComputeShader("ConvertRgbToNv12.hlsl").Empty());
    CHECK(!cache.GetComputeShader("FusedYuv420ToRgbResize.hlsl").Empty());
}

TEST(YuvHlslPrimitives, GpuProbeMatchesCpuGoldenValues) {
    REQUIRE_CORE(core);
    ProcessingFixture fixture(core);
    if (!fixture.context.SupportsRgba8Uav()) {
        TEST_SKIP("R8G8B8A8 UAV typed store is not supported");
    }

    const auto actual = ExecuteProbe(fixture);
    const auto expectedRgb = ExpectedProbeRgb();
    CHECK(actual.size() == expectedRgb.size() * 4u);

    for (size_t i = 0; i < expectedRgb.size(); ++i) {
        const uint8_t expected[4] = {
            ToByte(expectedRgb[i].x),
            ToByte(expectedRgb[i].y),
            ToByte(expectedRgb[i].z),
            255u,
        };
        for (size_t channel = 0; channel < 4; ++channel) {
            const int difference = std::abs(
                static_cast<int>(actual[i * 4u + channel]) -
                static_cast<int>(expected[channel]));
            if (difference > 2) {
                std::ostringstream os;
                os << "GPU golden mismatch at pixel=" << i
                   << " channel=" << channel
                   << " actual=" << static_cast<int>(actual[i * 4u + channel])
                   << " expected=" << static_cast<int>(expected[channel]);
                TEST_FAIL(os.str());
            }
        }
    }
}
