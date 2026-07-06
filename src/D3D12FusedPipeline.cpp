#include <D3D12Helper/D3D12Processing/D3D12FusedPipeline.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12FusedConstants {
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
static_assert((sizeof(D3D12FusedConstants) % 4) == 0, "root constants must be DWORD aligned");

UINT DivideRoundUp(UINT value, UINT divisor) noexcept {
    return (value + divisor - 1u) / divisor;
}

DXGI_FORMAT ResolveFormat(DXGI_FORMAT requested, const D3D12Resource& resource) {
    return requested == DXGI_FORMAT_UNKNOWN ? resource.GetFormat() : requested;
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

void ValidateFormatMatchesResource(DXGI_FORMAT requested, const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    const DXGI_FORMAT actual = resource.GetFormat();
    if (requested != DXGI_FORMAT_UNKNOWN && requested != actual) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " format does not match resource format";
        throw ValidationError(os.str());
    }
}

D3D12FusedConstants MakeConstants(const D3D12Resource& src, const D3D12Resource& dst, const FusedConvertResizeDesc& desc) {
    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;
    const ProcessingRect srcRect = ResolveRect(desc.srcRect, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);
    ValidateRectInside(srcRect, srcW, srcH, "D3D12FusedProcessor::RecordConvertResize", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12FusedProcessor::RecordConvertResize", "dstRect");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    if (IsYuv420Format(srcFormat)) {
        ValidateYuv420Rect(srcRect, "D3D12FusedProcessor::RecordConvertResize", "srcRect");
    }

    D3D12FusedConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.srcFormat = static_cast<UINT>(srcFormat);
    c.dstFormat = static_cast<UINT>(ResolveFormat(desc.dstFormat, dst));
    c.srcMatrix = static_cast<UINT>(desc.color.srcMatrix);
    c.srcRange = static_cast<UINT>(desc.color.srcRange);
    c.dstMatrix = static_cast<UINT>(desc.color.dstMatrix);
    c.dstRange = static_cast<UINT>(desc.color.dstRange);
    c.filter = static_cast<UINT>(desc.filter);
    c.alphaMode = static_cast<UINT>(desc.color.alphaMode);
    c.scaleX = static_cast<float>(srcRect.width) / static_cast<float>(dstRect.width);
    c.scaleY = static_cast<float>(srcRect.height) / static_cast<float>(dstRect.height);
    return c;
}

void TransitionForPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const D3D12ProcessingStateDesc& state) {

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto writeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const auto srcBefore = state.useExplicitStates ? state.srcBefore : src.GetState();
    const auto dstBefore = state.useExplicitStates ? state.dstBefore : dst.GetState();

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    if (srcBefore != readState) barriers[count++] = MakeTransitionBarrier(src.Get(), srcBefore, readState);
    if (dstBefore != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), dstBefore, writeState);
    if (count) commandContext.ResourceBarrier(count, barriers);

    if (!state.useExplicitStates) {
        src.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const D3D12ProcessingStateDesc& state) {

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto writeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));

    if (!state.useExplicitStates) return;

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    if (state.srcAfter != readState) barriers[count++] = MakeTransitionBarrier(src.Get(), readState, state.srcAfter);
    if (state.dstAfter != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    if (count) commandContext.ResourceBarrier(count, barriers);
}

void SetCommonComputeState(D3D12ProcessingContext& context, D3D12CommandContext& commandContext, const D3D12DescriptorRange& range) {
    if (!range.shaderVisible) {
        throw ValidationError("D3D12FusedProcessor: descriptor range must be shader-visible");
    }
    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12FusedProcessor: command context has no command list");
    }
    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(const D3D12ComputePipeline& pipeline, D3D12CommandContext& commandContext, const D3D12FusedConstants& constants) {
    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12FusedProcessor: pipeline has no root constants slot");
    }
    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12FusedConstants) / 4),
        &constants,
        0);
}

} // namespace

struct D3D12FusedProcessor::Pipelines {
    D3D12ComputePipeline rgbToRgbResize;
    D3D12ComputePipeline yuv420ToRgbResize;
    bool initialized = false;
};

D3D12FusedProcessor::D3D12FusedProcessor() = default;
D3D12FusedProcessor::~D3D12FusedProcessor() = default;
D3D12FusedProcessor::D3D12FusedProcessor(D3D12FusedProcessor&&) noexcept = default;
D3D12FusedProcessor& D3D12FusedProcessor::operator=(D3D12FusedProcessor&&) noexcept = default;

void D3D12FusedProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12FusedProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12FusedProcessor: processor is not initialized");
    }
}

void D3D12FusedProcessor::EnsurePipelines() {
    EnsureInitialized();
    if (!m_pipelines) m_pipelines.reset(new Pipelines());
    if (m_pipelines->initialized) return;

    ComputePipelineDesc oneSrvOneUav = {};
    oneSrvOneUav.numSrvs = 1;
    oneSrvOneUav.numUavs = 1;
    oneSrvOneUav.numRootConstantValues = static_cast<UINT>(sizeof(D3D12FusedConstants) / 4);

    ComputePipelineDesc twoSrvOneUav = {};
    twoSrvOneUav.numSrvs = 2;
    twoSrvOneUav.numUavs = 1;
    twoSrvOneUav.numRootConstantValues = oneSrvOneUav.numRootConstantValues;

    auto* device = m_context->GetDevice();
    m_pipelines->rgbToRgbResize.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("FusedRgbToRgbResize.hlsl"), oneSrvOneUav);
    m_pipelines->yuv420ToRgbResize.InitializeWithTemplate(
        device, m_shaderCache.GetComputeShader("FusedYuv420ToRgbResize.hlsl"), twoSrvOneUav);
    m_pipelines->initialized = true;
}

void D3D12FusedProcessor::RecordConvertResize(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FusedConvertResizeDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12FusedProcessor::RecordConvertResize", "src");
    ValidateTexture2D(dst, "D3D12FusedProcessor::RecordConvertResize", "dst");
    if (src.Get() == dst.Get()) {
        throw ValidationError("D3D12FusedProcessor::RecordConvertResize: in-place processing is not supported");
    }
    ValidateOutputUav(dst, "D3D12FusedProcessor::RecordConvertResize");
    ValidateFormatMatchesResource(desc.srcFormat, src, "D3D12FusedProcessor::RecordConvertResize", "src");
    ValidateFormatMatchesResource(desc.dstFormat, dst, "D3D12FusedProcessor::RecordConvertResize", "dst");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (IsRgbaLikeFormat(srcFormat) && IsRgbaLikeFormat(dstFormat)) {
        RecordRgbToRgbResize(commandContext, src, dst, desc, state);
    } else if (IsYuv420Format(srcFormat) && IsRgbaLikeFormat(dstFormat)) {
        RecordYuv420ToRgbResize(commandContext, src, dst, desc, state);
    } else {
        throw UnsupportedFormatError("D3D12FusedProcessor::RecordConvertResize: supported paths are RGBA-like -> RGBA-like and YUV420 -> RGBA-like");
    }
}

D3D12Resource D3D12FusedProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    if (width == 0 || height == 0) {
        throw ValidationError("D3D12FusedProcessor::CreateOutputTexture: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12FusedProcessor::CreateOutputTexture: only RGBA-like output formats are supported");
    }
    ValidateOutputFormatCaps(*m_context, format, "D3D12FusedProcessor::CreateOutputTexture");
    return CreateTexture2D(core, width, height, format, initialState, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

void D3D12FusedProcessor::RecordRgbToRgbResize(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FusedConvertResizeDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    const auto constants = MakeConstants(src, dst, desc);
    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, static_cast<DXGI_FORMAT>(constants.srcFormat));
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, static_cast<DXGI_FORMAT>(constants.dstFormat));

    TransitionForPass(commandContext, src, dst, state);
    SetCommonComputeState(*m_context, commandContext, srcViews.range);

    auto* cmd = commandContext.GetCommandList();
    m_pipelines->rgbToRgbResize.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->rgbToRgbResize.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    cmd->SetComputeRootDescriptorTable(m_pipelines->rgbToRgbResize.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->rgbToRgbResize, commandContext, constants);
    m_pipelines->rgbToRgbResize.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state);
}

void D3D12FusedProcessor::RecordYuv420ToRgbResize(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const FusedConvertResizeDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    const auto constants = MakeConstants(src, dst, desc);
    auto srcViews = CreateYuv420SrvViewSet(*m_context, src);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, static_cast<DXGI_FORMAT>(constants.dstFormat));

    TransitionForPass(commandContext, src, dst, state);
    SetCommonComputeState(*m_context, commandContext, srcViews.range);

    auto* cmd = commandContext.GetCommandList();
    m_pipelines->yuv420ToRgbResize.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->yuv420ToRgbResize.SrvTableIndex(), srcViews.Gpu(srcViews.ySrvIndex));
    cmd->SetComputeRootDescriptorTable(m_pipelines->yuv420ToRgbResize.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->yuv420ToRgbResize, commandContext, constants);
    m_pipelines->yuv420ToRgbResize.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state);
}

} // namespace Processing
} // namespace D3D12CoreLib
