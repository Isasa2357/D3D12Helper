#include <D3D12Helper/D3D12Processing/D3D12RegionEffect.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12RegionEffectConstants {
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
    UINT shape = 0;
    UINT selection = 0;

    UINT effect = 0;
    UINT reserved0 = 0;
    UINT reserved1 = 0;
    UINT reserved2 = 0;

    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float edgeSoftness = 0.0f;

    float rectX = 0.0f;
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;

    float strength = 1.0f;
    float reserved3 = 0.0f;
    float reserved4 = 0.0f;
    float reserved5 = 0.0f;

    float tintColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};
static_assert((sizeof(D3D12RegionEffectConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateRegionEffectDesc(const RegionEffectDesc& desc, const char* functionName) {
    if (desc.shape != RegionShape::Circle && desc.shape != RegionShape::Rect) {
        throw ValidationError(std::string(functionName) + ": unsupported region shape");
    }
    if (desc.selection != RegionSelection::Inside && desc.selection != RegionSelection::Outside) {
        throw ValidationError(std::string(functionName) + ": unsupported region selection");
    }
    if (desc.effect != RegionEffectMode::Darken &&
        desc.effect != RegionEffectMode::Tint &&
        desc.effect != RegionEffectMode::Grayscale &&
        desc.effect != RegionEffectMode::Highlight &&
        desc.effect != RegionEffectMode::AlphaFade &&
        desc.effect != RegionEffectMode::Vignette) {
        throw ValidationError(std::string(functionName) + ": unsupported region effect");
    }
    if (desc.shape == RegionShape::Circle && !(desc.radius > 0.0f)) {
        throw ValidationError(std::string(functionName) + ": circle radius must be greater than zero");
    }
    if (desc.shape == RegionShape::Rect && (!(desc.rectWidth > 0.0f) || !(desc.rectHeight > 0.0f))) {
        throw ValidationError(std::string(functionName) + ": rectWidth and rectHeight must be greater than zero");
    }
    if (desc.edgeSoftness < 0.0f) {
        throw ValidationError(std::string(functionName) + ": edgeSoftness must be non-negative");
    }
}

D3D12RegionEffectConstants MakeConstants(
    const D3D12Resource& src,
    const D3D12Resource& dst,
    const RegionEffectDesc& desc) {

    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;

    const ProcessingRect srcRect = ResolveRect(desc.srcRect, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);

    ValidateRectInside(srcRect, srcW, srcH, "D3D12RegionEffectProcessor::RecordRegionEffect", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12RegionEffectProcessor::RecordRegionEffect", "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12RegionEffectProcessor::RecordRegionEffect: region effect does not resize; srcRect and dstRect sizes must match");
    }

    const DXGI_FORMAT srcFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, dst);

    D3D12RegionEffectConstants c = {};
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
    c.shape = static_cast<UINT>(desc.shape);
    c.selection = static_cast<UINT>(desc.selection);
    c.effect = static_cast<UINT>(desc.effect);
    c.centerX = desc.centerX;
    c.centerY = desc.centerY;
    c.radius = desc.radius;
    c.edgeSoftness = desc.edgeSoftness;
    c.rectX = desc.rectX;
    c.rectY = desc.rectY;
    c.rectWidth = desc.rectWidth;
    c.rectHeight = desc.rectHeight;
    c.strength = desc.strength;
    std::copy(desc.tintColor, desc.tintColor + 4, c.tintColor);
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
        throw ValidationError("D3D12RegionEffectProcessor: descriptor range must be shader-visible");
    }

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12RegionEffectProcessor: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12RegionEffectConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12RegionEffectProcessor: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12RegionEffectConstants) / 4),
        &constants,
        0);
}

} // namespace

struct D3D12RegionEffectProcessor::Pipelines {
    D3D12ComputePipeline regionEffect;
    bool initialized = false;
};

D3D12RegionEffectProcessor::D3D12RegionEffectProcessor() = default;
D3D12RegionEffectProcessor::~D3D12RegionEffectProcessor() = default;
D3D12RegionEffectProcessor::D3D12RegionEffectProcessor(D3D12RegionEffectProcessor&&) noexcept = default;
D3D12RegionEffectProcessor& D3D12RegionEffectProcessor::operator=(D3D12RegionEffectProcessor&&) noexcept = default;

void D3D12RegionEffectProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12RegionEffectProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12RegionEffectProcessor: processor is not initialized");
    }
}

void D3D12RegionEffectProcessor::EnsurePipelines() {
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
    pipelineDesc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12RegionEffectConstants) / 4);

    m_pipelines->regionEffect.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("RegionEffectRgba.hlsl"),
        pipelineDesc);

    m_pipelines->initialized = true;
}

void D3D12RegionEffectProcessor::RecordRegionEffect(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const RegionEffectDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12RegionEffectProcessor::RecordRegionEffect";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRegionEffectDesc(desc, kFunction);

    const DXGI_FORMAT srcFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, dst);
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12RegionEffectProcessor::RecordRegionEffect: only RGBA-like formats are supported");
    }
    ValidateOutputFormatCaps(*m_context, dstFormat, kFunction);

    const auto constants = MakeConstants(src, dst, desc);

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, srcFormat);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);

    TransitionForPass(
        commandContext,
        src,
        dst,
        state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srcViews.range);
    m_pipelines->regionEffect.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        m_pipelines->regionEffect.SrvTableIndex(),
        srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        m_pipelines->regionEffect.UavTableIndex(),
        dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->regionEffect, commandContext, constants);

    m_pipelines->regionEffect.Dispatch(
        commandContext,
        DivideRoundUp(constants.dstWidth, kThreadGroupX),
        DivideRoundUp(constants.dstHeight, kThreadGroupY),
        1);

    TransitionAfterPass(
        commandContext,
        src,
        dst,
        state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

D3D12Resource D3D12RegionEffectProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12RegionEffectProcessor::CreateOutputTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12RegionEffectProcessor::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(*m_context, format, "D3D12RegionEffectProcessor::CreateOutputTexture");

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
