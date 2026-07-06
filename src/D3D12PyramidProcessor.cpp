#include <D3D12Helper/D3D12Processing/D3D12PyramidProcessor.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12PyramidConstants {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;

    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;

    UINT filter = 0;
    UINT edgeMode = 0;
    UINT reserved0 = 0;
    UINT reserved1 = 0;

    float borderColor[4] = { 0, 0, 0, 0 };
};
static_assert((sizeof(D3D12PyramidConstants) % 4) == 0, "root constants must be DWORD aligned");

UINT DivideRoundUp(UINT value, UINT divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

UINT HalfRoundUp(UINT value) noexcept {
    return value == 0 ? 0 : ((value + 1u) / 2u);
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

void ValidateNotSameResource(const D3D12Resource& a, const D3D12Resource& b, const char* functionName) {
    if (a.Get() == b.Get()) {
        std::ostringstream os;
        os << functionName << ": in-place processing is not supported";
        throw ValidationError(os.str());
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

void ValidateRgbaFormats(
    D3D12ProcessingContext& context,
    DXGI_FORMAT srcFormat,
    DXGI_FORMAT dstFormat,
    const char* functionName) {

    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError(std::string(functionName) + ": only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(context, dstFormat, functionName);
}

void ValidatePyramidEdgeMode(PyramidEdgeMode mode, const char* functionName) {
    switch (mode) {
    case PyramidEdgeMode::Clamp:
    case PyramidEdgeMode::Constant:
        return;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported pyramid edge mode");
    }
}

void ValidateProcessingFilter(ProcessingFilter filter, const char* functionName) {
    switch (filter) {
    case ProcessingFilter::Point:
    case ProcessingFilter::Linear:
        return;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported upsample filter");
    }
}

ProcessingRect ResolveAndValidateRect(
    const ProcessingRect& requested,
    const D3D12Resource& resource,
    const char* functionName,
    const char* argumentName) {

    const auto desc = resource.GetDesc();
    const auto rect = ResolveRect(requested, static_cast<UINT>(desc.Width), desc.Height);
    ValidateRectInside(rect, static_cast<UINT>(desc.Width), desc.Height, functionName, argumentName);
    return rect;
}

D3D12PyramidConstants MakeConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    UINT filter,
    PyramidEdgeMode edgeMode,
    const float borderColor[4]) {

    D3D12PyramidConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.filter = filter;
    c.edgeMode = static_cast<UINT>(edgeMode);
    std::copy(borderColor, borderColor + 4, c.borderColor);
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
        throw ValidationError("D3D12PyramidProcessor: descriptor range must be shader-visible");
    }

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12PyramidProcessor: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12PyramidConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12PyramidProcessor: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12PyramidConstants) / 4),
        &constants,
        0);
}

void BindAndDispatch(
    D3D12ProcessingContext& context,
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12TextureViewSet& srcViews,
    const D3D12TextureViewSet& dstViews,
    const D3D12PyramidConstants& constants) {

    SetCommonComputeState(context, commandContext, srcViews.range);
    pipeline.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(pipeline, commandContext, constants);

    pipeline.Dispatch(
        commandContext,
        DivideRoundUp(constants.dstWidth, kThreadGroupX),
        DivideRoundUp(constants.dstHeight, kThreadGroupY),
        1);
}

} // namespace

struct D3D12PyramidProcessor::Pipelines {
    D3D12ComputePipeline downsample2x;
    D3D12ComputePipeline upsample2x;
    bool initialized = false;
};

D3D12PyramidProcessor::D3D12PyramidProcessor() = default;
D3D12PyramidProcessor::~D3D12PyramidProcessor() = default;
D3D12PyramidProcessor::D3D12PyramidProcessor(D3D12PyramidProcessor&&) noexcept = default;
D3D12PyramidProcessor& D3D12PyramidProcessor::operator=(D3D12PyramidProcessor&&) noexcept = default;

void D3D12PyramidProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12PyramidProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12PyramidProcessor: processor is not initialized");
    }
}

void D3D12PyramidProcessor::EnsurePipelines() {
    EnsureInitialized();

    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.numSrvs = 1;
    pipelineDesc.numUavs = 1;
    pipelineDesc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12PyramidConstants) / 4);

    auto* device = m_context->GetDevice();
    m_pipelines->downsample2x.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("PyramidDownsample2xRgba.hlsl"),
        pipelineDesc);
    m_pipelines->upsample2x.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("PyramidUpsample2xRgba.hlsl"),
        pipelineDesc);
    m_pipelines->initialized = true;
}

void D3D12PyramidProcessor::RecordDownsample2x(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const PyramidDownsampleDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12PyramidProcessor::RecordDownsample2x";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidatePyramidEdgeMode(desc.edgeMode, kFunction);

    const DXGI_FORMAT srcFormat = src.GetFormat();
    const DXGI_FORMAT dstFormat = dst.GetFormat();
    ValidateRgbaFormats(*m_context, srcFormat, dstFormat, kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");

    const UINT expectedWidth = HalfRoundUp(srcRect.width);
    const UINT expectedHeight = HalfRoundUp(srcRect.height);
    if (dstRect.width != expectedWidth || dstRect.height != expectedHeight) {
        std::ostringstream os;
        os << kFunction << ": dstRect size must be ceil(srcRect / 2); expected "
           << expectedWidth << "x" << expectedHeight << " actual " << dstRect.width << "x" << dstRect.height;
        throw ValidationError(os.str());
    }

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, srcFormat);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);
    const auto constants = MakeConstants(srcRect, dstRect, 0, desc.edgeMode, desc.borderColor);

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindAndDispatch(*m_context, m_pipelines->downsample2x, commandContext, srcViews, dstViews, constants);
    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12PyramidProcessor::RecordUpsample2x(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const PyramidUpsampleDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12PyramidProcessor::RecordUpsample2x";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateProcessingFilter(desc.filter, kFunction);
    ValidatePyramidEdgeMode(desc.edgeMode, kFunction);

    const DXGI_FORMAT srcFormat = src.GetFormat();
    const DXGI_FORMAT dstFormat = dst.GetFormat();
    ValidateRgbaFormats(*m_context, srcFormat, dstFormat, kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");

    const UINT expectedWidth = srcRect.width * 2u;
    const UINT expectedHeight = srcRect.height * 2u;
    if (dstRect.width != expectedWidth || dstRect.height != expectedHeight) {
        std::ostringstream os;
        os << kFunction << ": dstRect size must be srcRect * 2; expected "
           << expectedWidth << "x" << expectedHeight << " actual " << dstRect.width << "x" << dstRect.height;
        throw ValidationError(os.str());
    }

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, srcFormat);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);
    const auto constants = MakeConstants(srcRect, dstRect, static_cast<UINT>(desc.filter), desc.edgeMode, desc.borderColor);

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    BindAndDispatch(*m_context, m_pipelines->upsample2x, commandContext, srcViews, dstViews, constants);
    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

D3D12Resource D3D12PyramidProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12PyramidProcessor::CreateOutputTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12PyramidProcessor::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(*m_context, format, "D3D12PyramidProcessor::CreateOutputTexture");

    return CreateTexture2D(
        core,
        width,
        height,
        format,
        initialState,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

D3D12Resource D3D12PyramidProcessor::CreateDownsampledTexture(
    D3D12Core& core,
    UINT srcWidth,
    UINT srcHeight,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    return CreateOutputTexture(core, HalfRoundUp(srcWidth), HalfRoundUp(srcHeight), format, initialState);
}

D3D12Resource D3D12PyramidProcessor::CreateUpsampledTexture(
    D3D12Core& core,
    UINT srcWidth,
    UINT srcHeight,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    if (srcWidth > (0xffffffffu / 2u) || srcHeight > (0xffffffffu / 2u)) {
        throw ValidationError("D3D12PyramidProcessor::CreateUpsampledTexture: size overflow");
    }

    return CreateOutputTexture(core, srcWidth * 2u, srcHeight * 2u, format, initialState);
}

} // namespace Processing
} // namespace D3D12CoreLib
