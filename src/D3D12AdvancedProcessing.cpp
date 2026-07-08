#include <D3D12Helper/D3D12Processing/D3D12AdvancedProcessing.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12AdvancedTransformConstants {
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
    UINT filter = 0;
    UINT borderMode = 0;

    float matrixRow0[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float matrixRow1[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    float matrixRow2[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static_assert((sizeof(D3D12AdvancedTransformConstants) % 4) == 0, "root constants must be DWORD aligned");

struct D3D12Lut3DConstants {
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
    UINT lutWidth = 0;
    UINT lutHeight = 0;

    UINT lutDepth = 0;
    UINT preserveAlpha = 1;
    float strength = 1.0f;
    UINT reserved0 = 0;
};
static_assert((sizeof(D3D12Lut3DConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateTexture3D(const D3D12Resource& resource, const char* functionName, const char* argumentName) {
    if (!resource.Get()) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is null";
        throw ValidationError(os.str());
    }
    const auto desc = resource.GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is not Texture3D";
        throw ValidationError(os.str());
    }
    if (desc.Width == 0 || desc.Height == 0 || desc.DepthOrArraySize == 0) {
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

void ValidateFilterAndBorder(ProcessingFilter filter, RemapBorderMode borderMode, const char* functionName) {
    if (filter != ProcessingFilter::Point && filter != ProcessingFilter::Linear) {
        std::ostringstream os;
        os << functionName << ": unsupported filter";
        throw ValidationError(os.str());
    }
    if (borderMode != RemapBorderMode::Clamp && borderMode != RemapBorderMode::Constant) {
        std::ostringstream os;
        os << functionName << ": unsupported border mode";
        throw ValidationError(os.str());
    }
}

void ValidateMatrixValues(const float* values, UINT count, const char* functionName) {
    for (UINT i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) {
            std::ostringstream os;
            os << functionName << ": matrix contains non-finite value";
            throw ValidationError(os.str());
        }
    }
}

void AddTransition(
    D3D12_RESOURCE_BARRIER* barriers,
    UINT& count,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {

    if (!resource || before == after) {
        return;
    }
    for (UINT i = 0; i < count; ++i) {
        if (barriers[i].Transition.pResource == resource) {
            return;
        }
    }
    barriers[count++] = MakeTransitionBarrier(resource, before, after);
}

void TransitionForOneInputPass(
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
    AddTransition(barriers, count, src.Get(), srcBefore, readState);
    AddTransition(barriers, count, dst.Get(), dstBefore, writeState);

    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }

    if (!state.useExplicitStates) {
        src.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterOneInputPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const D3D12ProcessingStateDesc& state) {

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto writeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));

    if (!state.useExplicitStates) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT count = 0;
    AddTransition(barriers, count, src.Get(), readState, state.srcAfter);
    AddTransition(barriers, count, dst.Get(), writeState, state.dstAfter);

    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }
}

void TransitionForTwoInputPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& dst,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto writeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    const auto src0Before = state.useExplicitStates ? state.src0Before : src0.GetState();
    const auto src1Before = state.useExplicitStates ? state.src1Before : src1.GetState();
    const auto dstBefore  = state.useExplicitStates ? state.dstBefore  : dst.GetState();

    D3D12_RESOURCE_BARRIER barriers[3] = {};
    UINT count = 0;
    AddTransition(barriers, count, src0.Get(), src0Before, readState);
    AddTransition(barriers, count, src1.Get(), src1Before, readState);
    AddTransition(barriers, count, dst.Get(),  dstBefore,  writeState);

    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }

    if (!state.useExplicitStates) {
        src0.SetState(readState);
        src1.SetState(readState);
        dst.SetState(writeState);
    }
}

void TransitionAfterTwoInputPass(
    D3D12CommandContext& commandContext,
    D3D12Resource& src0,
    D3D12Resource& src1,
    D3D12Resource& dst,
    const D3D12ProcessingTwoInputStateDesc& state) {

    constexpr auto readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto writeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));

    if (!state.useExplicitStates) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[3] = {};
    UINT count = 0;
    AddTransition(barriers, count, src0.Get(), readState, state.src0After);
    AddTransition(barriers, count, src1.Get(), readState, state.src1After);
    AddTransition(barriers, count, dst.Get(),  writeState, state.dstAfter);

    if (count > 0) {
        commandContext.ResourceBarrier(count, barriers);
    }
}

D3D12_SHADER_RESOURCE_VIEW_DESC MakeTexture2DSrvDesc(DXGI_FORMAT format) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.MipLevels = 1;
    desc.Texture2D.PlaneSlice = 0;
    desc.Texture2D.ResourceMinLODClamp = 0.0f;
    return desc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC MakeTexture3DSrvDesc(DXGI_FORMAT format) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture3D.MostDetailedMip = 0;
    desc.Texture3D.MipLevels = 1;
    desc.Texture3D.ResourceMinLODClamp = 0.0f;
    return desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeTexture2DUavDesc(DXGI_FORMAT format) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;
    return desc;
}

D3D12AdvancedTransformConstants MakeTransformConstants(
    const D3D12Resource& src,
    const D3D12Resource& dst,
    DXGI_FORMAT srcFormat,
    DXGI_FORMAT dstFormat,
    ProcessingFilter filter,
    RemapBorderMode borderMode,
    const float* matrix9,
    const float* borderColor,
    const ProcessingRect& srcRectIn,
    const ProcessingRect& dstRectIn,
    const char* functionName) {

    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcTexW = static_cast<UINT>(srcDesc.Width);
    const UINT srcTexH = srcDesc.Height;
    const UINT dstTexW = static_cast<UINT>(dstDesc.Width);
    const UINT dstTexH = dstDesc.Height;

    const auto srcRect = ResolveRect(srcRectIn, srcTexW, srcTexH);
    const auto dstRect = ResolveRect(dstRectIn, dstTexW, dstTexH);
    ValidateRectInside(srcRect, srcTexW, srcTexH, functionName, "srcRect");
    ValidateRectInside(dstRect, dstTexW, dstTexH, functionName, "dstRect");

    D3D12AdvancedTransformConstants c = {};
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
    c.filter = static_cast<UINT>(filter);
    c.borderMode = static_cast<UINT>(borderMode);
    c.matrixRow0[0] = matrix9[0];
    c.matrixRow0[1] = matrix9[1];
    c.matrixRow0[2] = matrix9[2];
    c.matrixRow1[0] = matrix9[3];
    c.matrixRow1[1] = matrix9[4];
    c.matrixRow1[2] = matrix9[5];
    c.matrixRow2[0] = matrix9[6];
    c.matrixRow2[1] = matrix9[7];
    c.matrixRow2[2] = matrix9[8];
    std::copy(borderColor, borderColor + 4, c.borderColor);
    return c;
}

D3D12Lut3DConstants MakeLutConstants(
    const D3D12Resource& src,
    const D3D12Resource& lut,
    const D3D12Resource& dst,
    const Lut3DDesc& desc) {

    const auto srcDesc = src.GetDesc();
    const auto lutDesc = lut.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const UINT srcTexW = static_cast<UINT>(srcDesc.Width);
    const UINT srcTexH = srcDesc.Height;
    const UINT dstTexW = static_cast<UINT>(dstDesc.Width);
    const UINT dstTexH = dstDesc.Height;

    const auto srcRect = ResolveRect(desc.srcRect, srcTexW, srcTexH);
    const auto dstRect = ResolveRect(desc.dstRect, dstTexW, dstTexH);
    ValidateRectInside(srcRect, srcTexW, srcTexH, "D3D12AdvancedProcessor::RecordApplyLut3D", "srcRect");
    ValidateRectInside(dstRect, dstTexW, dstTexH, "D3D12AdvancedProcessor::RecordApplyLut3D", "dstRect");

    D3D12Lut3DConstants c = {};
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
    c.lutWidth = desc.lutWidth ? desc.lutWidth : static_cast<UINT>(lutDesc.Width);
    c.lutHeight = desc.lutHeight ? desc.lutHeight : lutDesc.Height;
    c.lutDepth = desc.lutDepth ? desc.lutDepth : lutDesc.DepthOrArraySize;
    c.preserveAlpha = desc.preserveAlpha ? 1u : 0u;
    c.strength = desc.strength;
    return c;
}

} // namespace

struct D3D12AdvancedProcessor::Pipelines {
    D3D12ComputePipeline transform;
    D3D12ComputePipeline lut3d;
    bool initialized = false;
};

D3D12AdvancedProcessor::D3D12AdvancedProcessor() = default;
D3D12AdvancedProcessor::~D3D12AdvancedProcessor() = default;
D3D12AdvancedProcessor::D3D12AdvancedProcessor(D3D12AdvancedProcessor&&) noexcept = default;
D3D12AdvancedProcessor& D3D12AdvancedProcessor::operator=(D3D12AdvancedProcessor&&) noexcept = default;

void D3D12AdvancedProcessor::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_remapper.Initialize(context);
    m_pipelines.reset();
}

void D3D12AdvancedProcessor::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12AdvancedProcessor: processor is not initialized");
    }
}

void D3D12AdvancedProcessor::EnsurePipelines() {
    EnsureInitialized();
    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc transformDesc = {};
    transformDesc.numSrvs = 1;
    transformDesc.numUavs = 1;
    transformDesc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12AdvancedTransformConstants) / 4);
    m_pipelines->transform.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("AdvancedTransformRgba.hlsl"),
        transformDesc);

    ComputePipelineDesc lutDesc = {};
    lutDesc.numSrvs = 2;
    lutDesc.numUavs = 1;
    lutDesc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12Lut3DConstants) / 4);
    m_pipelines->lut3d.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("ApplyLut3D.hlsl"),
        lutDesc);

    m_pipelines->initialized = true;
}

void D3D12AdvancedProcessor::RecordAffineTransform(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const AffineTransformDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    float matrix9[9] = {
        desc.dstToSrc[0], desc.dstToSrc[1], desc.dstToSrc[2],
        desc.dstToSrc[3], desc.dstToSrc[4], desc.dstToSrc[5],
        0.0f, 0.0f, 1.0f,
    };

    PerspectiveTransformDesc perspective = {};
    perspective.srcFormat = desc.srcFormat;
    perspective.dstFormat = desc.dstFormat;
    perspective.filter = desc.filter;
    perspective.srcRect = desc.srcRect;
    perspective.dstRect = desc.dstRect;
    std::copy(matrix9, matrix9 + 9, perspective.dstToSrc);
    perspective.borderMode = desc.borderMode;
    std::copy(desc.borderColor, desc.borderColor + 4, perspective.borderColor);

    RecordPerspectiveTransform(commandContext, src, dst, perspective, state);
}

void D3D12AdvancedProcessor::RecordPerspectiveTransform(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& dst,
    const PerspectiveTransformDesc& desc,
    const D3D12ProcessingStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12AdvancedProcessor::RecordPerspectiveTransform", "src");
    ValidateTexture2D(dst, "D3D12AdvancedProcessor::RecordPerspectiveTransform", "dst");
    if (src.Get() == dst.Get()) {
        throw ValidationError("D3D12AdvancedProcessor::RecordPerspectiveTransform: in-place transform is not supported");
    }
    ValidateOutputUav(dst, "D3D12AdvancedProcessor::RecordPerspectiveTransform");
    ValidateFilterAndBorder(desc.filter, desc.borderMode, "D3D12AdvancedProcessor::RecordPerspectiveTransform");
    ValidateMatrixValues(desc.dstToSrc, 9, "D3D12AdvancedProcessor::RecordPerspectiveTransform");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12AdvancedProcessor::RecordPerspectiveTransform: only RGBA-like formats are supported");
    }

    const auto constants = MakeTransformConstants(
        src,
        dst,
        srcFormat,
        dstFormat,
        desc.filter,
        desc.borderMode,
        desc.dstToSrc,
        desc.borderColor,
        desc.srcRect,
        desc.dstRect,
        "D3D12AdvancedProcessor::RecordPerspectiveTransform");

    D3D12DescriptorRange srvRange = m_context->CbvSrvUavAllocator().AllocateRange(1);
    CreateSrv(m_context->Core(), src.Get(), MakeTexture2DSrvDesc(srcFormat), srvRange.Cpu(0));

    D3D12DescriptorRange uavRange = m_context->CbvSrvUavAllocator().AllocateRange(1);
    CreateUav(m_context->Core(), dst.Get(), MakeTexture2DUavDesc(dstFormat), uavRange.Cpu(0));

    TransitionForOneInputPass(commandContext, src, dst, state);

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12AdvancedProcessor::RecordPerspectiveTransform: command context has no command list");
    }
    ID3D12DescriptorHeap* heaps[] = { m_context->CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    m_pipelines->transform.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->transform.SrvTableIndex(), srvRange.Gpu(0));
    cmd->SetComputeRootDescriptorTable(m_pipelines->transform.UavTableIndex(), uavRange.Gpu(0));
    cmd->SetComputeRoot32BitConstants(
        m_pipelines->transform.RootConstantsIndex(),
        static_cast<UINT>(sizeof(D3D12AdvancedTransformConstants) / 4),
        &constants,
        0);
    m_pipelines->transform.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterOneInputPass(commandContext, src, dst, state);
}

void D3D12AdvancedProcessor::RecordApplyLut3D(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& lut,
    D3D12Resource& dst,
    const Lut3DDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12AdvancedProcessor::RecordApplyLut3D", "src");
    ValidateTexture3D(lut, "D3D12AdvancedProcessor::RecordApplyLut3D", "lut");
    ValidateTexture2D(dst, "D3D12AdvancedProcessor::RecordApplyLut3D", "dst");
    if (src.Get() == dst.Get() || lut.Get() == dst.Get()) {
        throw ValidationError("D3D12AdvancedProcessor::RecordApplyLut3D: in-place LUT application is not supported");
    }
    ValidateOutputUav(dst, "D3D12AdvancedProcessor::RecordApplyLut3D");
    ValidateOpacity(desc.strength, "D3D12AdvancedProcessor::RecordApplyLut3D");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    const DXGI_FORMAT lutFormat = ResolveFormat(desc.lutFormat, lut);
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat) || !IsRgbaLikeFormat(lutFormat)) {
        throw UnsupportedFormatError("D3D12AdvancedProcessor::RecordApplyLut3D: only RGBA-like src/lut/dst formats are supported");
    }

    const auto constants = MakeLutConstants(src, lut, dst, desc);
    if (constants.lutWidth == 0 || constants.lutHeight == 0 || constants.lutDepth == 0) {
        throw ValidationError("D3D12AdvancedProcessor::RecordApplyLut3D: LUT dimensions are zero");
    }

    D3D12DescriptorRange srvRange = m_context->CbvSrvUavAllocator().AllocateRange(2);
    CreateSrv(m_context->Core(), src.Get(), MakeTexture2DSrvDesc(srcFormat), srvRange.Cpu(0));
    CreateSrv(m_context->Core(), lut.Get(), MakeTexture3DSrvDesc(lutFormat), srvRange.Cpu(1));

    D3D12DescriptorRange uavRange = m_context->CbvSrvUavAllocator().AllocateRange(1);
    CreateUav(m_context->Core(), dst.Get(), MakeTexture2DUavDesc(dstFormat), uavRange.Cpu(0));

    TransitionForTwoInputPass(commandContext, src, lut, dst, state);

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12AdvancedProcessor::RecordApplyLut3D: command context has no command list");
    }
    ID3D12DescriptorHeap* heaps[] = { m_context->CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    m_pipelines->lut3d.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->lut3d.SrvTableIndex(), srvRange.Gpu(0));
    cmd->SetComputeRootDescriptorTable(m_pipelines->lut3d.UavTableIndex(), uavRange.Gpu(0));
    cmd->SetComputeRoot32BitConstants(
        m_pipelines->lut3d.RootConstantsIndex(),
        static_cast<UINT>(sizeof(D3D12Lut3DConstants) / 4),
        &constants,
        0);
    m_pipelines->lut3d.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterTwoInputPass(commandContext, src, lut, dst, state);
}

void D3D12AdvancedProcessor::RecordApplyUndistortMap(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& map,
    D3D12Resource& dst,
    const RemapDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    EnsureInitialized();
    m_remapper.RecordRemap(commandContext, src, map, dst, desc, state);
}

D3D12Resource D3D12AdvancedProcessor::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    if (width == 0 || height == 0) {
        throw ValidationError("D3D12AdvancedProcessor::CreateOutputTexture: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12AdvancedProcessor::CreateOutputTexture: only RGBA-like formats are supported");
    }
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !m_context->SupportsRgba8Uav()) {
        throw UnsupportedFeatureError("D3D12AdvancedProcessor::CreateOutputTexture: R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !m_context->SupportsBgra8Uav()) {
        throw UnsupportedFeatureError("D3D12AdvancedProcessor::CreateOutputTexture: B8G8R8A8 UAV typed store is not supported");
    }
    return CreateTexture2D(core, width, height, format, initialState, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
