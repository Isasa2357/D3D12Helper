#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;
constexpr UINT kPackedWeightCount = 20;

struct D3D12BlurConstants {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;

    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;

    UINT radius = 0;
    UINT edgeMode = 0;
    UINT reserved0 = 0;
    UINT reserved1 = 0;

    float borderColor[4] = { 0, 0, 0, 0 };
    float weights[kPackedWeightCount] = {};
};
static_assert((sizeof(D3D12BlurConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateOutputUav(const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    const auto desc = resource.GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " must have ALLOW_UNORDERED_ACCESS";
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

void ValidateRgbaUavSupport(D3D12ProcessingContext& context, DXGI_FORMAT format, const char* functionName) {
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

std::array<float, kPackedWeightCount> MakeBlurWeights(const BlurDesc& desc) {
    if (desc.radius > D3D12Blurrer::MaxRadius) {
        std::ostringstream os;
        os << "D3D12Blurrer::RecordBlur: radius must be <= " << D3D12Blurrer::MaxRadius;
        throw ValidationError(os.str());
    }

    std::array<float, kPackedWeightCount> weights = {};
    const UINT radius = desc.radius;

    if (radius == 0) {
        weights[0] = 1.0f;
        return weights;
    }

    switch (desc.mode) {
    case BlurMode::Box: {
        const float w = 1.0f / static_cast<float>(radius * 2u + 1u);
        for (UINT i = 0; i <= radius; ++i) {
            weights[i] = w;
        }
        return weights;
    }

    case BlurMode::Gaussian: {
        if (!(desc.sigma > 0.0f)) {
            throw ValidationError("D3D12Blurrer::RecordBlur: sigma must be greater than zero for Gaussian blur");
        }

        float sum = 0.0f;
        for (UINT i = 0; i <= radius; ++i) {
            const float x = static_cast<float>(i);
            const float w = std::exp(-(x * x) / (2.0f * desc.sigma * desc.sigma));
            weights[i] = w;
            sum += (i == 0) ? w : (2.0f * w);
        }

        if (!(sum > 0.0f)) {
            throw ValidationError("D3D12Blurrer::RecordBlur: invalid Gaussian weight sum");
        }

        for (UINT i = 0; i <= radius; ++i) {
            weights[i] /= sum;
        }
        return weights;
    }

    default:
        throw ValidationError("D3D12Blurrer::RecordBlur: unsupported blur mode");
    }
}

D3D12BlurConstants MakeBlurConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const BlurDesc& desc,
    const std::array<float, kPackedWeightCount>& weights) {

    D3D12BlurConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.radius = desc.radius;
    c.edgeMode = static_cast<UINT>(desc.edgeMode);
    std::copy(desc.borderColor, desc.borderColor + 4, c.borderColor);
    std::copy(weights.begin(), weights.end(), c.weights);
    return c;
}

void TransitionIfNeeded(
    D3D12CommandContext& commandContext,
    D3D12Resource& resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after,
    bool updateTrackedState) {

    if (before == after) {
        if (updateTrackedState) {
            resource.SetState(after);
        }
        return;
    }

    commandContext.ResourceBarrier(MakeTransitionBarrier(resource.Get(), before, after));
    if (updateTrackedState) {
        resource.SetState(after);
    }
}

void SetDescriptorHeap(D3D12ProcessingContext& context, D3D12CommandContext& commandContext) {
    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12Blurrer::RecordBlur: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12BlurConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12Blurrer::RecordBlur: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12BlurConstants) / 4),
        &constants,
        0);
}

void BindAndDispatch(
    D3D12ProcessingContext& context,
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12TextureViewSet& srcViews,
    UINT srvIndex,
    const D3D12TextureViewSet& dstViews,
    UINT uavIndex,
    const D3D12BlurConstants& constants) {

    SetDescriptorHeap(context, commandContext);
    pipeline.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        pipeline.SrvTableIndex(),
        srcViews.Gpu(srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        pipeline.UavTableIndex(),
        dstViews.Gpu(uavIndex));
    SetRootConstants(pipeline, commandContext, constants);
    pipeline.Dispatch(
        commandContext,
        DivideRoundUp(constants.dstWidth, kThreadGroupX),
        DivideRoundUp(constants.dstHeight, kThreadGroupY),
        1);
}

} // namespace

struct D3D12Blurrer::Pipelines {
    D3D12ComputePipeline horizontal;
    D3D12ComputePipeline vertical;
    bool initialized = false;
};

D3D12Blurrer::D3D12Blurrer() = default;
D3D12Blurrer::~D3D12Blurrer() = default;
D3D12Blurrer::D3D12Blurrer(D3D12Blurrer&&) noexcept = default;
D3D12Blurrer& D3D12Blurrer::operator=(D3D12Blurrer&&) noexcept = default;

void D3D12Blurrer::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12Blurrer::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12Blurrer: blurrer is not initialized");
    }
}

void D3D12Blurrer::EnsurePipelines() {
    EnsureInitialized();

    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc desc = {};
    desc.numSrvs = 1;
    desc.numUavs = 1;
    desc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12BlurConstants) / 4);

    auto* device = m_context->GetDevice();
    m_pipelines->horizontal.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("GaussianBlurHorizontalRgba.hlsl"),
        desc);
    m_pipelines->vertical.InitializeWithTemplate(
        device,
        m_shaderCache.GetComputeShader("GaussianBlurVerticalRgba.hlsl"),
        desc);
    m_pipelines->initialized = true;
}

void D3D12Blurrer::RecordBlur(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& scratch,
    D3D12Resource& dst,
    const BlurDesc& desc,
    const D3D12ProcessingBlurStateDesc& state) {

    EnsurePipelines();

    constexpr const char* kFunction = "D3D12Blurrer::RecordBlur";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(scratch, kFunction, "scratch");
    ValidateTexture2D(dst, kFunction, "dst");

    ValidateNotSameResource(src, scratch, kFunction);
    ValidateNotSameResource(src, dst, kFunction);
    ValidateNotSameResource(scratch, dst, kFunction);

    ValidateOutputUav(scratch, kFunction, "scratch");
    ValidateOutputUav(dst, kFunction, "dst");

    const DXGI_FORMAT srcFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, src);
    const DXGI_FORMAT scratchFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, scratch);
    const DXGI_FORMAT dstFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, dst);

    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(scratchFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12Blurrer::RecordBlur: only RGBA-like formats are supported");
    }

    ValidateRgbaUavSupport(*m_context, scratchFormat, kFunction);
    ValidateRgbaUavSupport(*m_context, dstFormat, kFunction);

    const auto srcDesc = src.GetDesc();
    const auto scratchDesc = scratch.GetDesc();
    const auto dstDesc = dst.GetDesc();

    const ProcessingRect srcRect = ResolveRect(desc.srcRect, static_cast<UINT>(srcDesc.Width), srcDesc.Height);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, static_cast<UINT>(dstDesc.Width), dstDesc.Height);

    ValidateRectInside(srcRect, static_cast<UINT>(srcDesc.Width), srcDesc.Height, kFunction, "srcRect");
    ValidateRectInside(dstRect, static_cast<UINT>(dstDesc.Width), dstDesc.Height, kFunction, "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12Blurrer::RecordBlur: blur does not resize; srcRect and dstRect sizes must match");
    }

    if (scratchDesc.Width < dstRect.width || scratchDesc.Height < dstRect.height) {
        throw ValidationError("D3D12Blurrer::RecordBlur: scratch texture is smaller than processing rect");
    }

    const auto weights = MakeBlurWeights(desc);

    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, srcFormat);
    auto scratchWriteViews = CreateRgbaTextureViewSet(*m_context, scratch, false, true, scratchFormat);

    const bool updateTrackedState = !state.useExplicitStates;

    const auto srcBefore = state.useExplicitStates ? state.srcBefore : src.GetState();
    const auto scratchBefore = state.useExplicitStates ? state.scratchBefore : scratch.GetState();

    TransitionIfNeeded(
        commandContext,
        src,
        srcBefore,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        updateTrackedState);
    TransitionIfNeeded(
        commandContext,
        scratch,
        scratchBefore,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        updateTrackedState);

    const ProcessingRect scratchRect = { 0, 0, dstRect.width, dstRect.height };
    auto horizontalConstants = MakeBlurConstants(srcRect, scratchRect, desc, weights);

    BindAndDispatch(
        *m_context,
        m_pipelines->horizontal,
        commandContext,
        srcViews,
        srcViews.srvIndex,
        scratchWriteViews,
        scratchWriteViews.uavIndex,
        horizontalConstants);

    commandContext.ResourceBarrier(MakeUavBarrier(scratch.Get()));

    auto scratchReadViews = CreateRgbaTextureViewSet(*m_context, scratch, true, false, scratchFormat);
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);

    const auto dstBefore = state.useExplicitStates ? state.dstBefore : dst.GetState();

    TransitionIfNeeded(
        commandContext,
        scratch,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        updateTrackedState);
    TransitionIfNeeded(
        commandContext,
        dst,
        dstBefore,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        updateTrackedState);

    auto verticalConstants = MakeBlurConstants(scratchRect, dstRect, desc, weights);

    BindAndDispatch(
        *m_context,
        m_pipelines->vertical,
        commandContext,
        scratchReadViews,
        scratchReadViews.srvIndex,
        dstViews,
        dstViews.uavIndex,
        verticalConstants);

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));

    if (state.useExplicitStates) {
        D3D12_RESOURCE_BARRIER barriers[3] = {};
        UINT count = 0;

        if (state.srcAfter != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            barriers[count++] = MakeTransitionBarrier(
                src.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                state.srcAfter);
        }
        if (state.scratchAfter != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            barriers[count++] = MakeTransitionBarrier(
                scratch.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                state.scratchAfter);
        }
        if (state.dstAfter != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            barriers[count++] = MakeTransitionBarrier(
                dst.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                state.dstAfter);
        }

        if (count > 0) {
            commandContext.ResourceBarrier(count, barriers);
        }
    }
}

D3D12Resource D3D12Blurrer::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12Blurrer::CreateOutputTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12Blurrer::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateRgbaUavSupport(*m_context, format, "D3D12Blurrer::CreateOutputTexture");

    return CreateTexture2D(
        core,
        width,
        height,
        format,
        initialState,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

D3D12Resource D3D12Blurrer::CreateScratchTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12Blurrer::CreateScratchTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12Blurrer::CreateScratchTexture: only RGBA-like formats are supported");
    }

    ValidateRgbaUavSupport(*m_context, format, "D3D12Blurrer::CreateScratchTexture");

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
