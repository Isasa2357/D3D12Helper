#include <D3D12Helper/D3D12Processing/D3D12FormatConverter.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12ProcessingConstants {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;
    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;
    UINT srcFormat = 0;
    UINT dstFormat = 0;
    UINT srcMatrix = 0;
    UINT srcRange = 0;
    UINT dstMatrix = 0;
    UINT dstRange = 0;
    UINT filter = 0;
    UINT alphaMode = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    UINT reserved0 = 0;
    UINT reserved1 = 0;
};
static_assert((sizeof(D3D12ProcessingConstants) % 4) == 0, "root constants must be DWORD aligned");

UINT DivideRoundUp(UINT value, UINT divisor) noexcept {
    return (value + divisor - 1u) / divisor;
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

DXGI_FORMAT ResolveFormat(DXGI_FORMAT requested, const D3D12Resource& resource) {
    return requested == DXGI_FORMAT_UNKNOWN ? resource.GetFormat() : requested;
}

void ValidateFormatMatchesResource(DXGI_FORMAT requested, const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    const DXGI_FORMAT actual = resource.GetFormat();
    if (requested != DXGI_FORMAT_UNKNOWN && requested != actual) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " format does not match resource format";
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
    if (format == DXGI_FORMAT_NV12 && !context.SupportsNv12Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": NV12 UAV plane views are not supported");
    }
    if (format == DXGI_FORMAT_P010 && !context.SupportsP010Uav()) {
        throw UnsupportedFeatureError(std::string(functionName) + ": P010 UAV plane views are not supported");
    }
}

D3D12ProcessingConstants MakeConstants(
    const D3D12Resource& src,
    const D3D12Resource& dst,
    const FormatConvertDesc& desc) {

    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;
    const ProcessingRect srcRect = ResolveRect(desc.srcRect, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);
    ValidateRectInside(srcRect, srcW, srcH, "D3D12FormatConverter::RecordConvert", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12FormatConverter::RecordConvert", "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12FormatConverter::RecordConvert: format conversion does not resize; use D3D12Resizer or D3D12FusedProcessor");
    }

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (IsYuv420Format(srcFormat)) {
        ValidateYuv420Rect(srcRect, "D3D12FormatConverter::RecordConvert", "srcRect");
    }
    if (IsYuv420Format(dstFormat)) {
        ValidateYuv420Rect(dstRect, "D3D12FormatConverter::RecordConvert", "dstRect");
    }

    D3D12ProcessingConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.srcFormat = static_cast<UINT>(srcFormat);
    c.dstFormat = static_cast<UINT>(dstFormat);
    c.srcMatrix = static_cast<UINT>(desc.color.srcMatrix);
    c.srcRange = static_cast<UINT>(desc.color.srcRange);
    c.dstMatrix = static_cast<UINT>(desc.color.dstMatrix);
    c.dstRange = static_cast<UINT>(desc.color.dstRange);
    c.filter = 0;
    c.alphaMode = static_cast<UINT>(desc.color.alphaMode);
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

    if (!state.useExplicitStates) return;

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    if (state.srcAfter != readState) barriers[count++] = MakeTransitionBarrier(src.Get(), readState, state.srcAfter);
    if (state.dstAfter != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);
}

void SetCommonComputeState(D3D12ProcessingContext& context, D3D12CommandContext& commandContext, const D3D12DescriptorRange& range) {
    if (!range.shaderVisible) {
        throw ValidationError("D3D12FormatConverter: descriptor range must be shader-visible");
    }
    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12FormatConverter: command context has no command list");
    }
    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(const D3D12ComputePipeline& pipeline, D3D12CommandContext& commandContext, const D3D12ProcessingConstants& constants) {
    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12FormatConverter: pipeline has no root constants slot");
    }
    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12ProcessingConstants) / 4),
        &constants,
        0);
}

} // namespace

struct D3D12FormatConverter::Pipelines {
    D3D12ComputePipeline rgbToRgb;
    D3D12ComputePipeline yuv420ToRgb;
    D3D12ComputePipeline rgbToYuv420;
    bool initialized = false;
};

D3D12FormatConverter::D3D12FormatConverter() = default;
D3D12FormatConverter::~D3D12FormatConverter() = default;
D3D12FormatConverter::D3D12FormatConverter(D3D12FormatConverter&&) noexcept = default;
D3D12FormatConverter& D3D12FormatConverter::operator=(D3D12FormatConverter&&) noexcept = default;

void D3D12FormatConverter::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12FormatConverter::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12FormatConverter: converter is not initialized");
    }
}

void D3D12FormatConverter::EnsurePipelines() {
    EnsureInitialized();
    if (!m_pipelines) m_pipelines.reset(new Pipelines());
    if (m_pipelines->initialized) return;

    ComputePipelineDesc oneSrvOneUav = {};
    oneSrvOneUav.numSrvs = 1;
    oneSrvOneUav.numUavs = 1;
    oneSrvOneUav.numRootConstantValues = static_cast<UINT>(sizeof(D3D12ProcessingConstants) / 4);

    ComputePipelineDesc twoSrvOneUav = {};
    twoSrvOneUav.numSrvs = 2;
    twoSrvOneUav.numUavs = 1;
    twoSrvOneUav.numRootConstantValues = oneSrvOneUav.numRootConstantValues;

    ComputePipelineDesc oneSrvTwoUav = {};
    oneSrvTwoUav.numSrvs = 1;
    oneSrvTwoUav.numUavs = 2;
    oneSrvTwoUav.numRootConstantValues = oneSrvOneUav.numRootConstantValues;

    auto* device = m_context->GetDevice();
    m_pipelines->rgbToRgb.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("ConvertRgbToRgb.hlsl"), oneSrvOneUav);
    m_pipelines->yuv420ToRgb.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("ConvertNv12ToRgb.hlsl"), twoSrvOneUav);
    m_pipelines->rgbToYuv420.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("ConvertRgbToNv12.hlsl"), oneSrvTwoUav);
    m_pipelines->initialized = true;
}

void D3D12FormatConverter::RecordConvert(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12FormatConverter::RecordConvert", "src");
    ValidateTexture2D(dst, "D3D12FormatConverter::RecordConvert", "dst");
    ValidateNotSameResource(src, dst, "D3D12FormatConverter::RecordConvert");
    ValidateOutputUav(dst, "D3D12FormatConverter::RecordConvert");
    ValidateFormatMatchesResource(desc.srcFormat, src, "D3D12FormatConverter::RecordConvert", "src");
    ValidateFormatMatchesResource(desc.dstFormat, dst, "D3D12FormatConverter::RecordConvert", "dst");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (!IsSupportedProcessingFormat(srcFormat) || !IsSupportedProcessingFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordConvert: unsupported format");
    }

    if (IsRgbaLikeFormat(srcFormat) && IsRgbaLikeFormat(dstFormat)) {
        RecordRgbToRgb(commandContext, src, dst, desc, state);
    } else if (IsYuv420Format(srcFormat) && IsRgbaLikeFormat(dstFormat)) {
        RecordYuv420ToRgb(commandContext, src, dst, desc, state);
    } else if (IsRgbaLikeFormat(srcFormat) && IsYuv420Format(dstFormat)) {
        RecordRgbToYuv420(commandContext, src, dst, desc, state);
    } else {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordConvert: unsupported conversion matrix");
    }
}

D3D12Resource D3D12FormatConverter::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    if (width == 0 || height == 0) {
        throw ValidationError("D3D12FormatConverter::CreateOutputTexture: size is zero");
    }
    if (!IsSupportedProcessingFormat(format)) {
        throw UnsupportedFormatError("D3D12FormatConverter::CreateOutputTexture: unsupported output format");
    }
    ValidateEvenSize(width, height, format, "D3D12FormatConverter::CreateOutputTexture");
    ValidateOutputFormatCaps(*m_context, format, "D3D12FormatConverter::CreateOutputTexture");
    return CreateTexture2D(core, width, height, format, initialState, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

void D3D12FormatConverter::RecordRgbToRgb(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    auto constants = MakeConstants(src, dst, desc);
    const DXGI_FORMAT srcFormat = static_cast<DXGI_FORMAT>(constants.srcFormat);
    const DXGI_FORMAT dstFormat = static_cast<DXGI_FORMAT>(constants.dstFormat);
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordRgbToRgb: expected RGBA-like formats");
    }

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, srcFormat);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->rgbToRgb.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToRgb.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToRgb.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->rgbToRgb, commandContext, constants);
    m_pipelines->rgbToRgb.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12FormatConverter::RecordYuv420ToRgb(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    auto constants = MakeConstants(src, dst, desc);
    if (!IsYuv420Format(static_cast<DXGI_FORMAT>(constants.srcFormat)) ||
        !IsRgbaLikeFormat(static_cast<DXGI_FORMAT>(constants.dstFormat))) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordYuv420ToRgb: expected YUV420 -> RGBA-like conversion");
    }

    auto srcViews = CreateYuv420SrvViewSet(*m_context, src);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, static_cast<DXGI_FORMAT>(constants.dstFormat));

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->yuv420ToRgb.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->yuv420ToRgb.SrvTableIndex(), srcViews.Gpu(srcViews.ySrvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->yuv420ToRgb.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->yuv420ToRgb, commandContext, constants);
    m_pipelines->yuv420ToRgb.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12FormatConverter::RecordRgbToYuv420(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    auto constants = MakeConstants(src, dst, desc);
    if (!IsRgbaLikeFormat(static_cast<DXGI_FORMAT>(constants.srcFormat)) ||
        !IsYuv420Format(static_cast<DXGI_FORMAT>(constants.dstFormat))) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordRgbToYuv420: expected RGBA-like -> YUV420 conversion");
    }
    ValidateEvenSize(constants.dstWidth, constants.dstHeight, static_cast<DXGI_FORMAT>(constants.dstFormat), "D3D12FormatConverter::RecordRgbToYuv420");

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, static_cast<DXGI_FORMAT>(constants.srcFormat));
    auto dstViews = CreateYuv420UavViewSet(*m_context, dst);

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->rgbToYuv420.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToYuv420.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToYuv420.UavTableIndex(), dstViews.Gpu(dstViews.yUavIndex));
    SetRootConstants(m_pipelines->rgbToYuv420, commandContext, constants);
    m_pipelines->rgbToYuv420.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
