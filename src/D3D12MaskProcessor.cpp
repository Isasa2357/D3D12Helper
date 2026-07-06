#include <D3D12Helper/D3D12Processing/D3D12MaskProcessor.hpp>

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

struct D3D12MaskConstants {
    UINT width = 0;
    UINT height = 0;
    INT srcX = 0;
    INT srcY = 0;

    INT maskX = 0;
    INT maskY = 0;
    INT dstX = 0;
    INT dstY = 0;

    INT overlayX = 0;
    INT overlayY = 0;
    UINT mode = 0;
    UINT channel = 0;

    UINT channelB = 0;
    UINT invert = 0;
    UINT invertB = 0;
    UINT reserved0 = 0;

    float strength = 1.0f;
    float opacity = 1.0f;
    float scale = 1.0f;
    float bias = 0.0f;
};
static_assert((sizeof(D3D12MaskConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateNotSameResource(const D3D12Resource& a, const D3D12Resource& b, const char* functionName) {
    if (a.Get() == b.Get()) {
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

void ValidateApplyDesc(const MaskApplyDesc& desc, const char* functionName) {
    if (desc.mode != MaskApplyMode::ApplyAlpha &&
        desc.mode != MaskApplyMode::MultiplyRgb &&
        desc.mode != MaskApplyMode::MultiplyRgba &&
        desc.mode != MaskApplyMode::ReplaceAlpha) {
        throw ValidationError(std::string(functionName) + ": unsupported mask apply mode");
    }
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.strength, functionName, "strength");
}

void ValidateBlendDesc(const MaskBlendDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
    ValidateFinite(desc.opacity, functionName, "opacity");
}

void ValidateCombineDesc(const MaskCombineDesc& desc, const char* functionName) {
    if (desc.mode != MaskCombineMode::Add &&
        desc.mode != MaskCombineMode::Multiply &&
        desc.mode != MaskCombineMode::Max &&
        desc.mode != MaskCombineMode::Min &&
        desc.mode != MaskCombineMode::Subtract) {
        throw ValidationError(std::string(functionName) + ": unsupported mask combine mode");
    }
    ValidateMaskChannel(desc.channelA, functionName, "channelA");
    ValidateMaskChannel(desc.channelB, functionName, "channelB");
    ValidateFinite(desc.scale, functionName, "scale");
    ValidateFinite(desc.bias, functionName, "bias");
}

void ValidateInvertDesc(const MaskInvertDesc& desc, const char* functionName) {
    ValidateMaskChannel(desc.channel, functionName, "channel");
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

D3D12MaskConstants MakeApplyConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& maskRect,
    const ProcessingRect& dstRect,
    const MaskApplyDesc& desc) {

    D3D12MaskConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.maskX = maskRect.x;
    c.maskY = maskRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.mode = static_cast<UINT>(desc.mode);
    c.channel = static_cast<UINT>(desc.channel);
    c.invert = desc.invert ? 1u : 0u;
    c.strength = desc.strength;
    return c;
}

D3D12MaskConstants MakeBlendConstants(
    const ProcessingRect& baseRect,
    const ProcessingRect& overlayRect,
    const ProcessingRect& maskRect,
    const ProcessingRect& dstRect,
    const MaskBlendDesc& desc) {

    D3D12MaskConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = baseRect.x;
    c.srcY = baseRect.y;
    c.maskX = maskRect.x;
    c.maskY = maskRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.overlayX = overlayRect.x;
    c.overlayY = overlayRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    c.invert = desc.invert ? 1u : 0u;
    c.opacity = desc.opacity;
    return c;
}

D3D12MaskConstants MakeCombineConstants(
    const ProcessingRect& maskARect,
    const ProcessingRect& maskBRect,
    const ProcessingRect& dstRect,
    const MaskCombineDesc& desc) {

    D3D12MaskConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.srcX = maskARect.x;
    c.srcY = maskARect.y;
    c.maskX = maskBRect.x;
    c.maskY = maskBRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.mode = static_cast<UINT>(desc.mode);
    c.channel = static_cast<UINT>(desc.channelA);
    c.channelB = static_cast<UINT>(desc.channelB);
    c.invert = desc.invertA ? 1u : 0u;
    c.invertB = desc.invertB ? 1u : 0u;
    c.scale = desc.scale;
    c.bias = desc.bias;
    return c;
}

D3D12MaskConstants MakeInvertConstants(
    const ProcessingRect& maskRect,
    const ProcessingRect& dstRect,
    const MaskInvertDesc& desc) {

    D3D12MaskConstants c = {};
    c.width = dstRect.width;
    c.height = dstRect.height;
    c.maskX = maskRect.x;
    c.maskY = maskRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.channel = static_cast<UINT>(desc.channel);
    return c;
}

void TransitionForOneInput(
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

void TransitionAfterOneInput(
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

void TransitionForTwoInputs(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& dst,
    const D3D12ProcessingTwoInputStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    const auto src0Before = state.useExplicitStates ? state.src0Before : src0.GetState();
    const auto src1Before = state.useExplicitStates ? state.src1Before : src1.GetState();
    const auto dstBefore = state.useExplicitStates ? state.dstBefore : dst.GetState();

    D3D12_RESOURCE_BARRIER barriers[3] = {};
    UINT count = 0;
    if (src0Before != readState) barriers[count++] = MakeTransitionBarrier(src0.Get(), src0Before, readState);
    if (src1Before != readState) barriers[count++] = MakeTransitionBarrier(src1.Get(), src1Before, readState);
    if (dstBefore != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), dstBefore, writeState);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);

    if (!state.useExplicitStates) {
        src0.SetState(readState);
        src1.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterTwoInputs(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& dst,
    const D3D12ProcessingTwoInputStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));
    if (!state.useExplicitStates) return;

    D3D12_RESOURCE_BARRIER barriers[3] = {};
    UINT count = 0;
    if (state.src0After != readState) barriers[count++] = MakeTransitionBarrier(src0.Get(), readState, state.src0After);
    if (state.src1After != readState) barriers[count++] = MakeTransitionBarrier(src1.Get(), readState, state.src1After);
    if (state.dstAfter != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);
}

void TransitionForThreeInputs(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& src2,
    D3D12Resource& dst,
    const D3D12ProcessingThreeInputStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    const auto src0Before = state.useExplicitStates ? state.src0Before : src0.GetState();
    const auto src1Before = state.useExplicitStates ? state.src1Before : src1.GetState();
    const auto src2Before = state.useExplicitStates ? state.src2Before : src2.GetState();
    const auto dstBefore = state.useExplicitStates ? state.dstBefore : dst.GetState();

    D3D12_RESOURCE_BARRIER barriers[4] = {};
    UINT count = 0;
    if (src0Before != readState) barriers[count++] = MakeTransitionBarrier(src0.Get(), src0Before, readState);
    if (src1Before != readState) barriers[count++] = MakeTransitionBarrier(src1.Get(), src1Before, readState);
    if (src2Before != readState) barriers[count++] = MakeTransitionBarrier(src2.Get(), src2Before, readState);
    if (dstBefore != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), dstBefore, writeState);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);

    if (!state.useExplicitStates) {
        src0.SetState(readState);
        src1.SetState(readState);
        src2.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterThreeInputs(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& src2,
    D3D12Resource& dst,
    const D3D12ProcessingThreeInputStateDesc& state,
    D3D12_RESOURCE_STATES readState,
    D3D12_RESOURCE_STATES writeState) {

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));
    if (!state.useExplicitStates) return;

    D3D12_RESOURCE_BARRIER barriers[4] = {};
    UINT count = 0;
    if (state.src0After != readState) barriers[count++] = MakeTransitionBarrier(src0.Get(), readState, state.src0After);
    if (state.src1After != readState) barriers[count++] = MakeTransitionBarrier(src1.Get(), readState, state.src1After);
    if (state.src2After != readState) barriers[count++] = MakeTransitionBarrier(src2.Get(), readState, state.src2After);
    if (state.dstAfter != writeState) barriers[count++] = MakeTransitionBarrier(dst.Get(), writeState, state.dstAfter);
    if (count > 0) commandContext.ResourceBarrier(count, barriers);
}

void SetCommonComputeState(D3D12ProcessingContext& context, D3D12CommandContext& commandContext, const D3D12DescriptorRange& range) {
    if (!range.shaderVisible) {
        throw ValidationError("D3D12MaskProcessor: descriptor range must be shader-visible");
    }

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12MaskProcessor: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12MaskConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12MaskProcessor: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12MaskConstants) / 4),
        &constants,
        0);
}

D3D12DescriptorRange CreateSrvTable(
    D3D12ProcessingContext& context,
    const D3D12Resource* const* resources,
    const DXGI_FORMAT* formats,
    UINT count) {

    auto range = context.CbvSrvUavAllocator().AllocateRange(count);
    if (!range.shaderVisible) {
        throw ValidationError("D3D12MaskProcessor: descriptor allocator must be shader-visible");
    }
    for (UINT i = 0; i < count; ++i) {
        CreateTexture2DSrv(context.Core(), *resources[i], range.Cpu(i), formats[i]);
    }
    return range;
}

D3D12DescriptorRange CreateUavTable(
    D3D12ProcessingContext& context,
    const D3D12Resource& resource,
    DXGI_FORMAT format) {

    auto range = context.CbvSrvUavAllocator().AllocateRange(1);
    if (!range.shaderVisible) {
        throw ValidationError("D3D12MaskProcessor: descriptor allocator must be shader-visible");
    }
    CreateTexture2DUav(context.Core(), resource, range.Cpu(0), format);
    return range;
}

} // namespace

struct D3D12MaskProcessor::Pipelines {
    D3D12ComputePipeline apply;
    D3D12ComputePipeline blend;
    D3D12ComputePipeline combine;
    D3D12ComputePipeline invert;
    bool initialized = false;
};

D3D12MaskProcessor::D3D12MaskProcessor() = default;
D3D12MaskProcessor::~D3D12MaskProcessor() = default;
D3D12MaskProcessor::D3D12MaskProcessor(D3D12MaskProcessor&&) noexcept = default;
D3D12MaskProcessor& D3D12MaskProcessor::operator=(D3D12MaskProcessor&&) noexcept = default;

void D3D12MaskProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12MaskProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12MaskProcessor: processor is not initialized");
    }
}

void D3D12MaskProcessor::EnsurePipelines() {
    EnsureInitialized();

    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc twoSrvOneUav = {};
    twoSrvOneUav.numSrvs = 2;
    twoSrvOneUav.numUavs = 1;
    twoSrvOneUav.numRootConstantValues = static_cast<UINT>(sizeof(D3D12MaskConstants) / 4);

    ComputePipelineDesc threeSrvOneUav = {};
    threeSrvOneUav.numSrvs = 3;
    threeSrvOneUav.numUavs = 1;
    threeSrvOneUav.numRootConstantValues = twoSrvOneUav.numRootConstantValues;

    ComputePipelineDesc oneSrvOneUav = {};
    oneSrvOneUav.numSrvs = 1;
    oneSrvOneUav.numUavs = 1;
    oneSrvOneUav.numRootConstantValues = twoSrvOneUav.numRootConstantValues;

    auto* device = m_context->GetDevice();
    m_pipelines->apply.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("MaskApplyRgba.hlsl"),
        twoSrvOneUav);
    m_pipelines->blend.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("MaskBlendRgba.hlsl"),
        threeSrvOneUav);
    m_pipelines->combine.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("MaskCombineRgba.hlsl"),
        twoSrvOneUav);
    m_pipelines->invert.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("MaskInvertRgba.hlsl"),
        oneSrvOneUav);

    m_pipelines->initialized = true;
}

void D3D12MaskProcessor::RecordApplyMask(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& mask,
    D3D12Resource& dst,
    const MaskApplyDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12MaskProcessor::RecordApplyMask";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(mask, kFunction, "mask");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateNotSameResource(mask, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(src, kFunction, "src");
    ValidateRgbaResource(mask, kFunction, "mask");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);
    ValidateApplyDesc(desc, kFunction);

    const auto srcRect = ResolveAndValidateRect(desc.srcRect, src, kFunction, "srcRect");
    const auto maskRect = ResolveAndValidateRect(desc.maskRect, mask, kFunction, "maskRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(srcRect, dstRect, kFunction, "srcRect", "dstRect");
    ValidateSameSize(maskRect, dstRect, kFunction, "maskRect", "dstRect");

    const auto constants = MakeApplyConstants(srcRect, maskRect, dstRect, desc);
    const D3D12Resource* srvs[] = { &src, &mask };
    const DXGI_FORMAT srvFormats[] = { ResolveFormat(DXGI_FORMAT_UNKNOWN, src), ResolveFormat(DXGI_FORMAT_UNKNOWN, mask) };
    auto srvTable = CreateSrvTable(*m_context, srvs, srvFormats, 2);
    auto uavTable = CreateUavTable(*m_context, dst, ResolveFormat(DXGI_FORMAT_UNKNOWN, dst));

    TransitionForTwoInputs(commandContext, src, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srvTable);
    m_pipelines->apply.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->apply.SrvTableIndex(), srvTable.Gpu(0));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->apply.UavTableIndex(), uavTable.Gpu(0));
    SetRootConstants(m_pipelines->apply, commandContext, constants);
    m_pipelines->apply.Dispatch(commandContext, DivideRoundUp(constants.width, kThreadGroupX), DivideRoundUp(constants.height, kThreadGroupY), 1);

    TransitionAfterTwoInputs(commandContext, src, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12MaskProcessor::RecordBlendByMask(
    D3D12CommandContext& commandContext,
    D3D12Resource& base,
    D3D12Resource& overlay,
    D3D12Resource& mask,
    D3D12Resource& dst,
    const MaskBlendDesc& desc,
    const D3D12ProcessingThreeInputStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12MaskProcessor::RecordBlendByMask";

    ValidateTexture2D(base, kFunction, "base");
    ValidateTexture2D(overlay, kFunction, "overlay");
    ValidateTexture2D(mask, kFunction, "mask");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(base, dst, kFunction);
    ValidateNotSameResource(overlay, dst, kFunction);
    ValidateNotSameResource(mask, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(base, kFunction, "base");
    ValidateRgbaResource(overlay, kFunction, "overlay");
    ValidateRgbaResource(mask, kFunction, "mask");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);
    ValidateBlendDesc(desc, kFunction);

    const auto baseRect = ResolveAndValidateRect(desc.baseRect, base, kFunction, "baseRect");
    const auto overlayRect = ResolveAndValidateRect(desc.overlayRect, overlay, kFunction, "overlayRect");
    const auto maskRect = ResolveAndValidateRect(desc.maskRect, mask, kFunction, "maskRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(baseRect, dstRect, kFunction, "baseRect", "dstRect");
    ValidateSameSize(overlayRect, dstRect, kFunction, "overlayRect", "dstRect");
    ValidateSameSize(maskRect, dstRect, kFunction, "maskRect", "dstRect");

    const auto constants = MakeBlendConstants(baseRect, overlayRect, maskRect, dstRect, desc);
    const D3D12Resource* srvs[] = { &base, &overlay, &mask };
    const DXGI_FORMAT srvFormats[] = { base.GetFormat(), overlay.GetFormat(), mask.GetFormat() };
    auto srvTable = CreateSrvTable(*m_context, srvs, srvFormats, 3);
    auto uavTable = CreateUavTable(*m_context, dst, dst.GetFormat());

    TransitionForThreeInputs(commandContext, base, overlay, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srvTable);
    m_pipelines->blend.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->blend.SrvTableIndex(), srvTable.Gpu(0));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->blend.UavTableIndex(), uavTable.Gpu(0));
    SetRootConstants(m_pipelines->blend, commandContext, constants);
    m_pipelines->blend.Dispatch(commandContext, DivideRoundUp(constants.width, kThreadGroupX), DivideRoundUp(constants.height, kThreadGroupY), 1);

    TransitionAfterThreeInputs(commandContext, base, overlay, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12MaskProcessor::RecordCombineMasks(
    D3D12CommandContext& commandContext,
    D3D12Resource& maskA,
    D3D12Resource& maskB,
    D3D12Resource& dst,
    const MaskCombineDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12MaskProcessor::RecordCombineMasks";

    ValidateTexture2D(maskA, kFunction, "maskA");
    ValidateTexture2D(maskB, kFunction, "maskB");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(maskA, dst, kFunction);
    ValidateNotSameResource(maskB, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(maskA, kFunction, "maskA");
    ValidateRgbaResource(maskB, kFunction, "maskB");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);
    ValidateCombineDesc(desc, kFunction);

    const auto maskARect = ResolveAndValidateRect(desc.maskARect, maskA, kFunction, "maskARect");
    const auto maskBRect = ResolveAndValidateRect(desc.maskBRect, maskB, kFunction, "maskBRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(maskARect, dstRect, kFunction, "maskARect", "dstRect");
    ValidateSameSize(maskBRect, dstRect, kFunction, "maskBRect", "dstRect");

    const auto constants = MakeCombineConstants(maskARect, maskBRect, dstRect, desc);
    const D3D12Resource* srvs[] = { &maskA, &maskB };
    const DXGI_FORMAT srvFormats[] = { maskA.GetFormat(), maskB.GetFormat() };
    auto srvTable = CreateSrvTable(*m_context, srvs, srvFormats, 2);
    auto uavTable = CreateUavTable(*m_context, dst, dst.GetFormat());

    TransitionForTwoInputs(commandContext, maskA, maskB, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srvTable);
    m_pipelines->combine.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->combine.SrvTableIndex(), srvTable.Gpu(0));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->combine.UavTableIndex(), uavTable.Gpu(0));
    SetRootConstants(m_pipelines->combine, commandContext, constants);
    m_pipelines->combine.Dispatch(commandContext, DivideRoundUp(constants.width, kThreadGroupX), DivideRoundUp(constants.height, kThreadGroupY), 1);

    TransitionAfterTwoInputs(commandContext, maskA, maskB, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void D3D12MaskProcessor::RecordInvertMask(
    D3D12CommandContext& commandContext,
    D3D12Resource& mask,
    D3D12Resource& dst,
    const MaskInvertDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12MaskProcessor::RecordInvertMask";

    ValidateTexture2D(mask, kFunction, "mask");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(mask, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateRgbaResource(mask, kFunction, "mask");
    ValidateRgbaResource(dst, kFunction, "dst");
    ValidateOutputFormatCaps(*m_context, dst.GetFormat(), kFunction);
    ValidateInvertDesc(desc, kFunction);

    const auto maskRect = ResolveAndValidateRect(desc.maskRect, mask, kFunction, "maskRect");
    const auto dstRect = ResolveAndValidateRect(desc.dstRect, dst, kFunction, "dstRect");
    ValidateSameSize(maskRect, dstRect, kFunction, "maskRect", "dstRect");

    const auto constants = MakeInvertConstants(maskRect, dstRect, desc);
    const D3D12Resource* srvs[] = { &mask };
    const DXGI_FORMAT srvFormats[] = { mask.GetFormat() };
    auto srvTable = CreateSrvTable(*m_context, srvs, srvFormats, 1);
    auto uavTable = CreateUavTable(*m_context, dst, dst.GetFormat());

    TransitionForOneInput(commandContext, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    SetCommonComputeState(*m_context, commandContext, srvTable);
    m_pipelines->invert.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->invert.SrvTableIndex(), srvTable.Gpu(0));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(m_pipelines->invert.UavTableIndex(), uavTable.Gpu(0));
    SetRootConstants(m_pipelines->invert, commandContext, constants);
    m_pipelines->invert.Dispatch(commandContext, DivideRoundUp(constants.width, kThreadGroupX), DivideRoundUp(constants.height, kThreadGroupY), 1);

    TransitionAfterOneInput(commandContext, mask, dst, state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

D3D12Resource D3D12MaskProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12MaskProcessor::CreateOutputTexture: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12MaskProcessor::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(*m_context, format, "D3D12MaskProcessor::CreateOutputTexture");

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
