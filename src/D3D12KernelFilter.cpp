#include <D3D12Helper/D3D12Processing/D3D12KernelFilter.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12KernelFilterConstants {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;

    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;

    UINT edgeMode = 0;
    UINT preserveAlpha = 1;
    UINT reserved0 = 0;
    UINT reserved1 = 0;

    float scale = 1.0f;
    float bias = 0.0f;
    float reserved2 = 0.0f;
    float reserved3 = 0.0f;

    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    float kernel0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float kernel1[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    float kernel2[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert((sizeof(D3D12KernelFilterConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateKernelFilterDesc(const KernelFilterDesc& desc, const char* functionName) {
    if (desc.mode != KernelFilterMode::Custom3x3 &&
        desc.mode != KernelFilterMode::Sharpen &&
        desc.mode != KernelFilterMode::EdgeDetect) {
        throw ValidationError(std::string(functionName) + ": unsupported kernel filter mode");
    }

    if (desc.edgeMode != KernelEdgeMode::Clamp &&
        desc.edgeMode != KernelEdgeMode::Constant) {
        throw ValidationError(std::string(functionName) + ": unsupported edge mode");
    }

    ValidateFinite(desc.scale, functionName, "scale");
    ValidateFinite(desc.bias, functionName, "bias");

    for (UINT i = 0; i < 9; ++i) {
        if (!IsFinite(desc.kernel[i])) {
            std::ostringstream os;
            os << functionName << ": kernel[" << i << "] must be finite";
            throw ValidationError(os.str());
        }
    }

    for (UINT i = 0; i < 4; ++i) {
        if (!IsFinite(desc.borderColor[i])) {
            std::ostringstream os;
            os << functionName << ": borderColor[" << i << "] must be finite";
            throw ValidationError(os.str());
        }
    }
}

void BuildKernel(const KernelFilterDesc& desc, float outKernel[9], float& outScale, float& outBias) {
    switch (desc.mode) {
    case KernelFilterMode::Custom3x3:
        std::copy(desc.kernel, desc.kernel + 9, outKernel);
        outScale = desc.scale;
        outBias = desc.bias;
        return;

    case KernelFilterMode::Sharpen: {
        const float k[9] = {
             0.0f, -1.0f,  0.0f,
            -1.0f,  5.0f, -1.0f,
             0.0f, -1.0f,  0.0f,
        };
        std::copy(k, k + 9, outKernel);
        outScale = 1.0f;
        outBias = 0.0f;
        return;
    }

    case KernelFilterMode::EdgeDetect: {
        const float k[9] = {
            -1.0f, -1.0f, -1.0f,
            -1.0f,  8.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
        };
        std::copy(k, k + 9, outKernel);
        outScale = 1.0f;
        outBias = 0.0f;
        return;
    }

    default:
        throw ValidationError("D3D12KernelFilter::RecordKernelFilter: unsupported kernel filter mode");
    }
}

D3D12KernelFilterConstants MakeConstants(
    const D3D12Resource& src,
    const D3D12Resource& dst,
    const KernelFilterDesc& desc) {

    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();

    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;

    const ProcessingRect srcRect = ResolveRect(desc.srcRect, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);

    ValidateRectInside(srcRect, srcW, srcH, "D3D12KernelFilter::RecordKernelFilter", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12KernelFilter::RecordKernelFilter", "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12KernelFilter::RecordKernelFilter: kernel filter does not resize; srcRect and dstRect sizes must match");
    }

    float kernel[9] = {};
    float scale = 1.0f;
    float bias = 0.0f;
    BuildKernel(desc, kernel, scale, bias);

    D3D12KernelFilterConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.edgeMode = static_cast<UINT>(desc.edgeMode);
    c.preserveAlpha = desc.preserveAlpha ? 1u : 0u;
    c.scale = scale;
    c.bias = bias;
    std::copy(desc.borderColor, desc.borderColor + 4, c.borderColor);

    c.kernel0[0] = kernel[0];
    c.kernel0[1] = kernel[1];
    c.kernel0[2] = kernel[2];
    c.kernel0[3] = kernel[3];

    c.kernel1[0] = kernel[4];
    c.kernel1[1] = kernel[5];
    c.kernel1[2] = kernel[6];
    c.kernel1[3] = kernel[7];

    c.kernel2[0] = kernel[8];
    c.kernel2[1] = 0.0f;
    c.kernel2[2] = 0.0f;
    c.kernel2[3] = 0.0f;

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
        throw ValidationError("D3D12KernelFilter: descriptor range must be shader-visible");
    }

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12KernelFilter: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12KernelFilterConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12KernelFilter: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12KernelFilterConstants) / 4),
        &constants,
        0);
}

} // namespace

struct D3D12KernelFilter::Pipelines {
    D3D12ComputePipeline kernelFilter;
    bool initialized = false;
};

D3D12KernelFilter::D3D12KernelFilter() = default;
D3D12KernelFilter::~D3D12KernelFilter() = default;
D3D12KernelFilter::D3D12KernelFilter(D3D12KernelFilter&&) noexcept = default;
D3D12KernelFilter& D3D12KernelFilter::operator=(D3D12KernelFilter&&) noexcept = default;

void D3D12KernelFilter::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12KernelFilter::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12KernelFilter: filter is not initialized");
    }
}

void D3D12KernelFilter::EnsurePipelines() {
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
    pipelineDesc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12KernelFilterConstants) / 4);

    m_pipelines->kernelFilter.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("KernelFilterRgba.hlsl"),
        pipelineDesc);

    m_pipelines->initialized = true;
}

void D3D12KernelFilter::RecordKernelFilter(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const KernelFilterDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();

    constexpr const char* kFunction = "D3D12KernelFilter::RecordKernelFilter";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction);
    ValidateKernelFilterDesc(desc, kFunction);

    const DXGI_FORMAT srcFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(DXGI_FORMAT_UNKNOWN, dst);

    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12KernelFilter::RecordKernelFilter: only RGBA-like formats are supported");
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
    m_pipelines->kernelFilter.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        m_pipelines->kernelFilter.SrvTableIndex(),
        srcViews.Gpu(srcViews.srvIndex));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(
        m_pipelines->kernelFilter.UavTableIndex(),
        dstViews.Gpu(dstViews.uavIndex));
    SetRootConstants(m_pipelines->kernelFilter, commandContext, constants);

    m_pipelines->kernelFilter.Dispatch(
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

D3D12Resource D3D12KernelFilter::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12KernelFilter::CreateOutputTexture: size is zero");
    }

    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12KernelFilter::CreateOutputTexture: only RGBA-like formats are supported");
    }

    ValidateOutputFormatCaps(*m_context, format, "D3D12KernelFilter::CreateOutputTexture");

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
