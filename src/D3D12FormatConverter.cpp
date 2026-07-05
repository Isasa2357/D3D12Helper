#include "D3D12Processing/D3D12FormatConverter.hpp"

#include "D3D12Core/D3D12Barrier.hpp"
#include "D3D12Core/D3D12FormatUtil.hpp"
#include "D3D12Framework/D3D12Helpers.hpp"

#include <algorithm>
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
        throw ValidationError("D3D12FormatConverter::RecordConvert: format conversion does not resize; use D3D12Resizer first");
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
    c.srcFormat = static_cast<UINT>(ResolveFormat(desc.srcFormat, src));
    c.dstFormat = static_cast<UINT>(ResolveFormat(desc.dstFormat, dst));
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
    if (srcBefore != readState) {
        barriers[count++] = MakeTransitionBarrier(src.Get(), srcBefore, readState);
    }
    if (dstBefore != writeState) {
        barriers[count++] = MakeTransitionBarrier(dst.Get(), dstBefore, writeState);
    }
    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }

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
    if (state.srcAfter != readState) {
        barriers[count++] = MakeTransitionBarrier(src.Get(), readState, state.srcAfter);
    }
    if (state.dstAfter != writeState) {
        barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    }
    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }
}

void SetCommonComputeState(
    D3D12ProcessingContext& context,
    D3D12CommandContext& commandContext,
    const D3D12DescriptorRange& range) {

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

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12ProcessingConstants& constants) {

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
    D3D12ComputePipeline nv12ToRgb;
    D3D12ComputePipeline rgbToNv12;
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
    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

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
    m_pipelines->nv12ToRgb.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("ConvertNv12ToRgb.hlsl"), twoSrvOneUav);
    m_pipelines->rgbToNv12.InitializeWithTemplate(
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

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (!IsSupportedProcessingFormat(srcFormat) || !IsSupportedProcessingFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordConvert: unsupported format");
    }

    if (IsRgbaLikeFormat(srcFormat) && IsRgbaLikeFormat(dstFormat)) {
        RecordRgbToRgb(commandContext, src, dst, desc, state);
    } else if (srcFormat == DXGI_FORMAT_NV12 && IsRgbaLikeFormat(dstFormat)) {
        RecordNv12ToRgb(commandContext, src, dst, desc, state);
    } else if (IsRgbaLikeFormat(srcFormat) && dstFormat == DXGI_FORMAT_NV12) {
        RecordRgbToNv12(commandContext, src, dst, desc, state);
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
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !m_context->SupportsRgba8Uav()) {
        throw UnsupportedFeatureError("D3D12FormatConverter::CreateOutputTexture: R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !m_context->SupportsBgra8Uav()) {
        throw UnsupportedFeatureError("D3D12FormatConverter::CreateOutputTexture: B8G8R8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_NV12 && !m_context->SupportsNv12Uav()) {
        throw UnsupportedFeatureError("D3D12FormatConverter::CreateOutputTexture: NV12 UAV plane views are not supported");
    }
    return CreateTexture2D(
        core,
        width,
        height,
        format,
        initialState,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
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

void D3D12FormatConverter::RecordNv12ToRgb(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    auto constants = MakeConstants(src, dst, desc);
    if (static_cast<DXGI_FORMAT>(constants.srcFormat) != DXGI_FORMAT_NV12 ||
        !IsRgbaLikeFormat(static_cast<DXGI_FORMAT>(constants.dstFormat))) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordNv12ToRgb: expected NV12 -> RGBA-like conversion");
    }
    ValidateEvenSize(constants.srcWidth, constants.srcHeight, DXGI_FORMAT_NV12, "D3D12FormatConverter::RecordNv12ToRgb");

    auto srcViews = CreateNv12SrvViewSet(*m_context, src);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, static_cast<DXGI_FORMAT>(constants.dstFormat));

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->nv12ToRgb.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->nv12ToRgb.SrvTableIndex(), srcViews.Gpu(srcViews.ySrvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->nv12ToRgb.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->nv12ToRgb, commandContext, constants);
    m_pipelines->nv12ToRgb.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12FormatConverter::RecordRgbToNv12(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FormatConvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    auto constants = MakeConstants(src, dst, desc);
    if (!IsRgbaLikeFormat(static_cast<DXGI_FORMAT>(constants.srcFormat)) ||
        static_cast<DXGI_FORMAT>(constants.dstFormat) != DXGI_FORMAT_NV12) {
        throw UnsupportedFormatError("D3D12FormatConverter::RecordRgbToNv12: expected RGBA-like -> NV12 conversion");
    }
    ValidateEvenSize(constants.dstWidth, constants.dstHeight, DXGI_FORMAT_NV12, "D3D12FormatConverter::RecordRgbToNv12");

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, static_cast<DXGI_FORMAT>(constants.srcFormat));
    auto dstViews = CreateNv12UavViewSet(*m_context, dst);

    TransitionForPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->rgbToNv12.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToNv12.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->rgbToNv12.UavTableIndex(), dstViews.Gpu(dstViews.yUavIndex));
    SetRootConstants(m_pipelines->rgbToNv12, commandContext, constants);
    m_pipelines->rgbToNv12.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
