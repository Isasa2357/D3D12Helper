#include <D3D12Helper/D3D12Processing/D3D12ThresholdProcessor.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12ThresholdConstants {
    UINT width = 0;
    UINT height = 0;
    INT srcX = 0;
    INT srcY = 0;

    INT dstX = 0;
    INT dstY = 0;
    UINT channel = 0;
    UINT invert = 0;

    float threshold = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float opacity = 1.0f;

    UINT mode = 0;
    UINT classCount = 16;
    float classScale = 255.0f;
    UINT reserved0 = 0;

    float foregroundColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float backgroundColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float overlayColor[4] = { 1.0f, 0.0f, 0.0f, 0.5f };
};
static_assert((sizeof(D3D12ThresholdConstants) % 4) == 0, "root constants must be DWORD aligned");

UINT DivideRoundUp(UINT value, UINT divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

bool IsFinite(float v) noexcept {
    return std::isfinite(v) != 0;
}

void ValidateFinite(float v, const char* functionName, const char* fieldName) {
    if (!IsFinite(v)) {
        std::ostringstream os;
        os << functionName << ": " << fieldName << " must be finite";
        throw ValidationError(os.str());
    }
}

void ValidateTexture2D(const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    if (!resource.Get()) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is null";
        throw ValidationError(os.str());
    }

    const auto desc = resource.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is not Texture2D";
        throw ValidationError(os.str());
    }

    if (desc.Width == 0 || desc.Height == 0) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " has zero size";
        throw ValidationError(os.str());
    }
}

void ValidateOutputUav(const D3D12Resource& resource, const char* functionName) {
    const auto desc = resource.GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        std::ostringstream os;
        os << functionName << ": destination texture must have ALLOW_UNORDERED_ACCESS";
        throw ValidationError(os.str());
    }
}

void ValidateNotSameResource(const D3D12Resource& src, const D3D12Resource& dst, const char* functionName) {
    if (src.Get() == dst.Get()) {
        std::ostringstream os;
        os << functionName << ": in-place processing is not supported";
        throw ValidationError(os.str());
    }
}

void ValidateRgbaResource(const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    if (!IsRgbaLikeFormat(resource.GetFormat())) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " must be RGBA-like";
        throw UnsupportedFormatError(os.str());
    }
}

void ValidateOutputFormatCaps(D3D12ProcessingContext& context, DXGI_FORMAT format, const char* functionName) {
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !context.SupportsRgba8Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !context.SupportsBgra8Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": B8G8R8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT && !context.SupportsRgba16FloatUav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": R16G16B16A16_FLOAT UAV typed store is not supported");
    }
}

void ValidateMaskChannel(MaskChannel channel, const char* functionName, const char* fieldName) {
    if (channel != MaskChannel::Red &&
        channel != MaskChannel::Green &&
        channel != MaskChannel::Blue &&
        channel != MaskChannel::Alpha &&
        channel != MaskChannel::Luma) {
        std::ostringstream os;
        os << functionName << ": unsupported " << fieldName;
        throw ValidationError(os.str());
    }
}

void ValidateHeatmapMode(HeatmapMode mode, const char* functionName) {
    if (mode != HeatmapMode::Grayscale &&
        mode != HeatmapMode::RedGreen &&
        mode != HeatmapMode::BlueRed &&
        mode != HeatmapMode::TurboApprox) {
        throw ValidationError(std::string(functionName) + ": unsupported heatmap mode");
    }
}

void ValidateSameSize(const ProcessingRect& a, const ProcessingRect& b, const char* functionName, const char* aName, const char* bName) {
    if (a.width != b.width || a.height != b.height) {
        std::ostringstream os;
        os << functionName << ": " << aName << " and " << bName << " sizes must match";
        throw ValidationError(os.str());
    }
}

ProcessingRect ResolveAndValidateRect(
    const ProcessingRect& requested,
    const D3D12Resource& resource,
    const char* functionName,
    const char* argumentName) {

    const auto desc = resource.GetDesc();
    const ProcessingRect rect = ResolveRect(requested, static_cast<UINT>(desc.Width), desc.Height);
    ValidateRectInside(rect, static_cast<UINT>(desc.Width), desc.Height, functionName, argumentName);
    return rect;
}

void ValidateThresholdDesc(const ThresholdDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.threshold, functionName, "threshold");
    for (float v : desc.foregroundColor) ValidateFinite(v, functionName, "foregroundColor");
    for (float v : desc.backgroundColor) ValidateFinite(v, functionName, "backgroundColor");
}

void ValidateRangeThresholdDesc(const RangeThresholdDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.minValue, functionName, "minValue");
    ValidateFinite(desc.maxValue, functionName, "maxValue");
    if (desc.maxValue < desc.minValue) {
        throw ValidationError(std::string(functionName) + ": maxValue must be >= minValue");
    }
    for (float v : desc.foregroundColor) ValidateFinite(v, functionName, "foregroundColor");
    for (float v : desc.backgroundColor) ValidateFinite(v, functionName, "backgroundColor");
}

void ValidateHeatmapDesc(const ConfidenceHeatmapDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateHeatmapMode(desc.mode, functionName);
    ValidateFinite(desc.minValue, functionName, "minValue");
    ValidateFinite(desc.maxValue, functionName, "maxValue");
    ValidateFinite(desc.opacity, functionName, "opacity");
    if (desc.maxValue <= desc.minValue) {
        throw ValidationError(std::string(functionName) + ": maxValue must be > minValue");
    }
}

void ValidateClassColorMapDesc(const ClassColorMapDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.classScale, functionName, "classScale");
    ValidateFinite(desc.opacity, functionName, "opacity");
    if (desc.classCount == 0) {
        throw ValidationError(std::string(functionName) + ": classCount must be greater than zero");
    }
}

void ValidateMaskOverlayDesc(const MaskOverlayDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.opacity, functionName, "opacity");
    for (float v : desc.overlayColor) ValidateFinite(v, functionName, "overlayColor");
}

D3D12ThresholdConstants MakeThresholdConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const ThresholdDesc& desc) {

    D3D12ThresholdConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.invert = desc.invert ? 1u : 0u;
    c.threshold = desc.threshold;
    std::copy(desc.foregroundColor, desc.foregroundColor + 4, c.foregroundColor);
    std::copy(desc.backgroundColor, desc.backgroundColor + 4, c.backgroundColor);
    return c;
}

D3D12ThresholdConstants MakeRangeThresholdConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const RangeThresholdDesc& desc) {

    D3D12ThresholdConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.invert = desc.invert ? 1u : 0u;
    c.minValue = desc.minValue;
    c.maxValue = desc.maxValue;
    std::copy(desc.foregroundColor, desc.foregroundColor + 4, c.foregroundColor);
    std::copy(desc.backgroundColor, desc.backgroundColor + 4, c.backgroundColor);
    return c;
}

D3D12ThresholdConstants MakeHeatmapConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const ConfidenceHeatmapDesc& desc) {

    D3D12ThresholdConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.minValue = desc.minValue;
    c.maxValue = desc.maxValue;
    c.opacity = desc.opacity;
    c.mode = static_cast<UINT>(desc.mode);
    return c;
}

D3D12ThresholdConstants MakeClassColorMapConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const ClassColorMapDesc& desc) {

    D3D12ThresholdConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.classScale = desc.classScale;
    c.classCount = desc.classCount;
    c.opacity = desc.opacity;
    return c;
}

D3D12ThresholdConstants MakeMaskOverlayConstants(
    const ProcessingRect& maskRect,
    const ProcessingRect& dstRect,
    const MaskOverlayDesc& desc) {

    D3D12ThresholdConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = maskRect.x;
    c.srcY = maskRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.invert = desc.invert ? 1u : 0u;
    c.opacity = desc.opacity;
    std::copy(desc.overlayColor, desc.overlayColor + 4, c.overlayColor);
    return c;
}

void TransitionForPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const D3D12ProcessingStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    const auto srcBefore = state.useExplicitStates ? state.srcBefore : src.GetState();
    const auto dstBefore = state.useExplicitStates ? state.dstBefore : dst.GetState();

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    if (srcBefore != readState) barriers[count++] = MakeTransitionBarrier(src.Get(), srcBefore, readState);
    if (dstBefore != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), dstBefore, writeState);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);

    if (!state.useExplicitStates) {
        src.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const D3D12ProcessingStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));

    if (!state.useExplicitStates) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    if (state.srcAfter != readState) barriers[count++] = MakeTransitionBarrier(src.Get(), readState, state.srcAfter);
    if (state.dstAfter != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);
}

void SetCommonComputeState(D3D12ProcessingContext& context, D3D12CommandContext& commandContext, const D3D12DescriptorRange& range) {
    if (!range.shaderVisible) {
        throw ValidationError("D3D12ThresholdProcessor: descriptor range must be shader-visible");
    }

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12ThresholdProcessor: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12ThresholdConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12ThresholdProcessor: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12ThresholdConstants) / 4),
        &constants,
        0);
}

void RecordOneInputPass(
    D3D12ProcessingContext& context,
    D3D12CommandContext& commandContext,
    const D3D12ComputePipeline& pipeline,
    D3D12Resource& src,
    D3D12Resource& dst,
    DXGI_FORMAT srcFormat,
    DXGI_FORMAT dstFormat,
    const D3D12ThresholdConstants& constants,
    const D3D12ProcessingStateDesc& state) {

    auto srcViews = CreateRgbaTextureViewSet(context, src, true, false, srcFormat);
    auto dstViews = CreateRgbaTextureViewSet(context, dst, false, true, dstFormat);

    TransitionForPass(
        commandContext,
        src,
        dst,
        state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(context, commandContext, srcViews.range);
    pipeline.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        pipeline.SrvTableIndex(),
        srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        pipeline.UavTableIndex(),
        dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(pipeline, commandContext, constants);

    pipeline.Dispatch(
        commandContext,
        DivideRoundUp(constants.width, kThreadGroupX),
        DivideRoundUp(constants.height, kThreadGroupY),
        1);

    TransitionAfterPass(
        commandContext,
        src,
        dst,
        state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

} // namespace

struct D3D12ThresholdProcessor::Pipelines {
    D3D12ComputePipeline threshold;
    D3D12ComputePipeline rangeThreshold;
    D3D12ComputePipeline heatmap;
    D3D12ComputePipeline classColorMap;
    D3D12ComputePipeline maskOverlay;
    bool initialized = false;
};

D3D12ThresholdProcessor::D3D12ThresholdProcessor() = default;
D3D12ThresholdProcessor::~D3D12ThresholdProcessor() = default;
D3D12ThresholdProcessor::D3D12ThresholdProcessor(D3D12ThresholdProcessor&&) noexcept = default;
D3D12ThresholdProcessor& D3D12ThresholdProcessor::operator=(D3D12ThresholdProcessor&&) noexcept = default;

void D3D12ThresholdProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12ThresholdProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12ThresholdProcessor: processor is not initialized");
    }
}

void D3D12ThresholdProcessor::EnsurePipelines() {
    EnsureInitialized();

    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc oneSrvOneUav = {};
    oneSrvOneUav.numSrvs = 1;
    oneSrvOneUav.numUavs = 1;
    oneSrvOneUav.numRootConstantValues = static_cast<UINT>(sizeof(D3D12ThresholdConstants) / 4);

    auto* device = m_context->GetDevice();
    m_pipelines->threshold.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("ThresholdRgba.hlsl"),
        oneSrvOneUav);
    m_pipelines->rangeThreshold.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("RangeThresholdRgba.hlsl"),
        oneSrvOneUav);
    m_pipelines->heatmap.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("ConfidenceHeatmapRgba.hlsl"),
        oneSrvOneUav);
    m_pipelines->classColorMap.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("ClassColorMapRgba.hlsl"),
        oneSrvOneUav);
    m_pipelines->maskOverlay.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("MaskOverlayRgba.hlsl"),
        oneSrvOneUav);

    m_pipelines->initialized = true;
}

void D3D12ThresholdProcessor::RecordThreshold(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const ThresholdDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12ThresholdProcessor::RecordThreshold";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(src, kFunction, "src");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateThresholdDesc(desc, kFunction);
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(srcRect, dstRect, kFunction, "srcRect", "dstRect");

    const auto constants = MakeThresholdConstants(srcRect, dstRect, desc);
    RecordOneInputPass(*m_context, commandContext, m_pipelines->threshold, src, dst, src.GetFormat(), dst.GetFormat(), constants, state);
}

void D3D12ThresholdProcessor::RecordRangeThreshold(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const RangeThresholdDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12ThresholdProcessor::RecordRangeThreshold";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(src, kFunction, "src");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateRangeThresholdDesc(desc, kFunction);
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(srcRect, dstRect, kFunction, "srcRect", "dstRect");

    const auto constants = MakeRangeThresholdConstants(srcRect, dstRect, desc);
    RecordOneInputPass(*m_context, commandContext, m_pipelines->rangeThreshold, src, dst, src.GetFormat(), dst.GetFormat(), constants, state);
}

void D3D12ThresholdProcessor::RecordConfidenceHeatmap(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const ConfidenceHeatmapDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12ThresholdProcessor::RecordConfidenceHeatmap";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(src, kFunction, "src");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateHeatmapDesc(desc, kFunction);
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(srcRect, dstRect, kFunction, "srcRect", "dstRect");

    const auto constants = MakeHeatmapConstants(srcRect, dstRect, desc);
    RecordOneInputPass(*m_context, commandContext, m_pipelines->heatmap, src, dst, src.GetFormat(), dst.GetFormat(), constants, state);
}

void D3D12ThresholdProcessor::RecordClassColorMap(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const ClassColorMapDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12ThresholdProcessor::RecordClassColorMap";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(src, kFunction, "src");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateClassColorMapDesc(desc, kFunction);
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(srcRect, dstRect, kFunction, "srcRect", "dstRect");

    const auto constants = MakeClassColorMapConstants(srcRect, dstRect, desc);
    RecordOneInputPass(*m_context, commandContext, m_pipelines->classColorMap, src, dst, src.GetFormat(), dst.GetFormat(), constants, state);
}

void D3D12ThresholdProcessor::RecordMaskOverlay(
    D3D12CommandContext& commandContext,
    D3D12Resource& mask,
    D3D12Resource& dst,
    const MaskOverlayDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12ThresholdProcessor::RecordMaskOverlay";

    ValidateTexture2D(mask, kFunction, "mask");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(mask, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(mask, kFunction, "mask");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateMaskOverlayDesc(desc, kFunction);
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);

    const auto maskRect = ResolveAndValidateRect(desc.maskRect, mask, kFunction, "maskRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(maskRect, dstRect, kFunction, "maskRect", "dstRect");

    const auto constants = MakeMaskOverlayConstants(maskRect, dstRect, desc);
    RecordOneInputPass(*m_context, commandContext, m_pipelines->maskOverlay, mask, dst, mask.GetFormat(), dst.GetFormat(), constants, state);
}

D3D12Resource D3D12ThresholdProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12ThresholdProcessor::CreateOutputTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12ThresholdProcessor::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(*m_context, format, "D3D12ThresholdProcessor::CreateOutputTexture");

    return CreateTexture2D(
        core,
        width,
        height,
        format,
        initialState,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
