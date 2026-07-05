#include "D3D12Processing/D3D12Resize.hpp"

#include "D3D12Core/D3D12Barrier.hpp"
#include "D3D12Framework/D3D12Helpers.hpp"

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12ResizeConstants {
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
static_assert((sizeof(D3D12ResizeConstants) % 4) == 0, "root constants must be DWORD aligned");

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
}

void ValidateOutputUav(const D3D12Resource& resource, const char* functionName) {
    const auto desc = resource.GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        std::ostringstream os;
        os << functionName << ": destination texture must have ALLOW_UNORDERED_ACCESS";
        throw ValidationError(os.str());
    }
}

D3D12ResizeConstants MakeConstants(const D3D12Resource& src, const D3D12Resource& dst, const ResizeDesc& desc) {
    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;
    const ProcessingRect srcRect = ResolveRect(desc.srcRect, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);
    ValidateRectInside(srcRect, srcW, srcH, "D3D12Resizer::RecordResize", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12Resizer::RecordResize", "dstRect");

    D3D12ResizeConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.srcFormat = static_cast<UINT>(src.GetFormat());
    c.dstFormat = static_cast<UINT>(dst.GetFormat());
    c.filter = static_cast<UINT>(desc.filter);
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

} // namespace

struct D3D12Resizer::Pipelines {
    D3D12ComputePipeline resize;
    bool initialized = false;
};

D3D12Resizer::D3D12Resizer() = default;
D3D12Resizer::~D3D12Resizer() = default;
D3D12Resizer::D3D12Resizer(D3D12Resizer&&) noexcept = default;
D3D12Resizer& D3D12Resizer::operator=(D3D12Resizer&&) noexcept = default;

void D3D12Resizer::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12Resizer::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12Resizer: resizer is not initialized");
    }
}

void D3D12Resizer::EnsurePipelines() {
    EnsureInitialized();
    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc pd = {};
    pd.numSrvs = 1;
    pd.numUavs = 1;
    pd.numRootConstantValues = static_cast<UINT>(sizeof(D3D12ResizeConstants) / 4);
    m_pipelines->resize.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("ResizeRgba.hlsl"),
        pd);
    m_pipelines->initialized = true;
}

void D3D12Resizer::RecordResize(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const ResizeDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12Resizer::RecordResize", "src");
    ValidateTexture2D(dst, "D3D12Resizer::RecordResize", "dst");
    if (src.Get() == dst.Get()) {
        throw ValidationError("D3D12Resizer::RecordResize: in-place resize is not supported");
    }
    ValidateOutputUav(dst, "D3D12Resizer::RecordResize");
    if (!IsRgbaLikeFormat(src.GetFormat()) || !IsRgbaLikeFormat(dst.GetFormat())) {
        throw UnsupportedFormatError("D3D12Resizer::RecordResize: only RGBA-like formats are supported");
    }

    const auto constants = MakeConstants(src, dst, desc);
    auto srcViews = CreateRgbaTextureViewSet(*m_context, src, true, false, src.GetFormat());
    auto dstViews = CreateRgbaTextureViewSet(*m_context, dst, false, true, dst.GetFormat());

    TransitionForPass(commandContext, src, dst, state);

    ID3D12DescriptorHeap* heaps[] = { m_context->CbvSrvUavAllocator().GetHeap() };
    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12Resizer::RecordResize: command context has no command list");
    }
    cmd->SetDescriptorHeaps(1, heaps);

    m_pipelines->resize.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->resize.SrvTableIndex(), srcViews.Gpu(srcViews.srvIndex));
    cmd->SetComputeRootDescriptorTable(m_pipelines->resize.UavTableIndex(), dstViews.Gpu(dstViews.uavIndex));
    cmd->SetComputeRoot32BitConstants(
        m_pipelines->resize.RootConstantsIndex(),
        static_cast<UINT>(sizeof(D3D12ResizeConstants) / 4),
        &constants,
        0);
    m_pipelines->resize.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, dst, state);
}

D3D12Resource D3D12Resizer::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    if (width == 0 || height == 0) {
        throw ValidationError("D3D12Resizer::CreateOutputTexture: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12Resizer::CreateOutputTexture: only RGBA-like formats are supported");
    }
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !m_context->SupportsRgba8Uav()) {
        throw UnsupportedFeatureError("D3D12Resizer::CreateOutputTexture: R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !m_context->SupportsBgra8Uav()) {
        throw UnsupportedFeatureError("D3D12Resizer::CreateOutputTexture: B8G8R8A8 UAV typed store is not supported");
    }
    return CreateTexture2D(core, width, height, format, initialState, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
