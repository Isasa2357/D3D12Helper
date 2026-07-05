#include "D3D12Processing/D3D12Remap.hpp"

#include "D3D12Core/D3D12Barrier.hpp"
#include "D3D12Framework/D3D12Helpers.hpp"

#include <sstream>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12RemapConstants {
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
    UINT mapWidth = 0;
    UINT mapHeight = 0;
    UINT coordinateMode = 0;
    UINT borderMode = 0;
    INT overlayX = 0;
    INT overlayY = 0;
    UINT overlayFormat = 0;
    UINT blendMode = 0;
    float opacity = 1.0f;
    UINT reserved2 = 0;
    UINT reserved3 = 0;
    UINT reserved4 = 0;
    float borderColor[4] = { 0, 0, 0, 0 };
};
static_assert((sizeof(D3D12RemapConstants) % 4) == 0, "root constants must be DWORD aligned");

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

DXGI_FORMAT ResolveFormat(DXGI_FORMAT requested, const D3D12Resource& resource) {
    return requested == DXGI_FORMAT_UNKNOWN ? resource.GetFormat() : requested;
}

void ValidateRemapDescEnums(const RemapDesc& desc) {
    if (desc.filter != ProcessingFilter::Point && desc.filter != ProcessingFilter::Linear) {
        throw ValidationError("D3D12Remapper::RecordRemap: unsupported filter");
    }
    if (desc.coordinateMode != RemapCoordinateMode::AbsolutePixels &&
        desc.coordinateMode != RemapCoordinateMode::NormalizedZeroToOne) {
        throw ValidationError("D3D12Remapper::RecordRemap: unsupported coordinate mode");
    }
    if (desc.borderMode != RemapBorderMode::Clamp &&
        desc.borderMode != RemapBorderMode::Constant) {
        throw ValidationError("D3D12Remapper::RecordRemap: unsupported border mode");
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

void TransitionForPass(
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

void TransitionAfterPass(
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

D3D12_UNORDERED_ACCESS_VIEW_DESC MakeTexture2DUavDesc(DXGI_FORMAT format) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;
    return desc;
}

D3D12RemapConstants MakeConstants(
    const D3D12Resource& src,
    const D3D12Resource& map,
    const D3D12Resource& dst,
    const RemapDesc& desc) {

    const auto srcDesc = src.GetDesc();
    const auto mapDesc = map.GetDesc();
    const auto dstDesc = dst.GetDesc();

    const UINT srcW = static_cast<UINT>(srcDesc.Width);
    const UINT srcH = srcDesc.Height;
    const UINT mapW = static_cast<UINT>(mapDesc.Width);
    const UINT mapH = mapDesc.Height;
    const UINT dstW = static_cast<UINT>(dstDesc.Width);
    const UINT dstH = dstDesc.Height;

    const ProcessingRect srcRect = ResolveRect({}, srcW, srcH);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, dstW, dstH);
    ValidateRectInside(srcRect, srcW, srcH, "D3D12Remapper::RecordRemap", "srcRect");
    ValidateRectInside(dstRect, dstW, dstH, "D3D12Remapper::RecordRemap", "dstRect");

    if (dstRect.width > mapW || dstRect.height > mapH) {
        throw ValidationError("D3D12Remapper::RecordRemap: map texture is smaller than destination rect");
    }

    D3D12RemapConstants c = {};
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
    c.filter = static_cast<UINT>(desc.filter);
    c.mapWidth = dstRect.width;
    c.mapHeight = dstRect.height;
    c.coordinateMode = static_cast<UINT>(desc.coordinateMode);
    c.borderMode = static_cast<UINT>(desc.borderMode);
    c.opacity = 1.0f;
    c.borderColor[0] = desc.borderColor[0];
    c.borderColor[1] = desc.borderColor[1];
    c.borderColor[2] = desc.borderColor[2];
    c.borderColor[3] = desc.borderColor[3];
    return c;
}

} // namespace

struct D3D12Remapper::Pipelines {
    D3D12ComputePipeline remap;
    bool initialized = false;
};

D3D12Remapper::D3D12Remapper() = default;
D3D12Remapper::~D3D12Remapper() = default;
D3D12Remapper::D3D12Remapper(D3D12Remapper&&) noexcept = default;
D3D12Remapper& D3D12Remapper::operator=(D3D12Remapper&&) noexcept = default;

void D3D12Remapper::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pipelines.reset();
}

void D3D12Remapper::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12Remapper: remapper is not initialized");
    }
}

void D3D12Remapper::EnsurePipelines() {
    EnsureInitialized();
    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc pd = {};
    pd.numSrvs = 2;
    pd.numUavs = 1;
    pd.numRootConstantValues = static_cast<UINT>(sizeof(D3D12RemapConstants) / 4);
    m_pipelines->remap.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("RemapRgba.hlsl"),
        pd);
    m_pipelines->initialized = true;
}

void D3D12Remapper::RecordRemap(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12Resource& map,
    D3D12Resource& dst,
    const RemapDesc& desc,
    const D3D12ProcessingTwoInputStateDesc& state) {

    EnsurePipelines();
    ValidateTexture2D(src, "D3D12Remapper::RecordRemap", "src");
    ValidateTexture2D(map, "D3D12Remapper::RecordRemap", "map");
    ValidateTexture2D(dst, "D3D12Remapper::RecordRemap", "dst");
    if (src.Get() == dst.Get() || map.Get() == dst.Get()) {
        throw ValidationError("D3D12Remapper::RecordRemap: in-place remap is not supported");
    }
    ValidateOutputUav(dst, "D3D12Remapper::RecordRemap");
    ValidateRemapDescEnums(desc);

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    const DXGI_FORMAT mapFormat = ResolveFormat(desc.mapFormat, map);

    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError("D3D12Remapper::RecordRemap: only RGBA-like src/dst formats are supported");
    }
    if (!IsSupportedRemapMapFormat(mapFormat)) {
        throw UnsupportedFormatError("D3D12Remapper::RecordRemap: map format must be R32G32_FLOAT");
    }

    const auto constants = MakeConstants(src, map, dst, desc);

    D3D12DescriptorRange srvRange = m_context->CbvSrvUavAllocator().AllocateRange(2);
    CreateSrv(m_context->Core(), src.Get(), MakeTexture2DSrvDesc(srcFormat), srvRange.Cpu(0));
    CreateSrv(m_context->Core(), map.Get(), MakeTexture2DSrvDesc(mapFormat), srvRange.Cpu(1));

    D3D12DescriptorRange uavRange = m_context->CbvSrvUavAllocator().AllocateRange(1);
    CreateUav(m_context->Core(), dst.Get(), MakeTexture2DUavDesc(dstFormat), uavRange.Cpu(0));

    TransitionForPass(commandContext, src, map, dst, state);

    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12Remapper::RecordRemap: command context has no command list");
    }
    ID3D12DescriptorHeap* heaps[] = { m_context->CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    m_pipelines->remap.Bind(commandContext);
    cmd->SetComputeRootDescriptorTable(m_pipelines->remap.SrvTableIndex(), srvRange.Gpu(0));
    cmd->SetComputeRootDescriptorTable(m_pipelines->remap.UavTableIndex(), uavRange.Gpu(0));
    cmd->SetComputeRoot32BitConstants(
        m_pipelines->remap.RootConstantsIndex(),
        static_cast<UINT>(sizeof(D3D12RemapConstants) / 4),
        &constants,
        0);
    m_pipelines->remap.Dispatch(commandContext, DivideRoundUp(constants.dstWidth, kThreadGroupX), DivideRoundUp(constants.dstHeight, kThreadGroupY), 1);

    TransitionAfterPass(commandContext, src, map, dst, state);
}

D3D12Resource D3D12Remapper::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    if (width == 0 || height == 0) {
        throw ValidationError("D3D12Remapper::CreateOutputTexture: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12Remapper::CreateOutputTexture: only RGBA-like formats are supported");
    }
    if (format == DXGI_FORMAT_R8G8B8A8_UNORM && !m_context->SupportsRgba8Uav()) {
        throw UnsupportedFeatureError("D3D12Remapper::CreateOutputTexture: R8G8B8A8 UAV typed store is not supported");
    }
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM && !m_context->SupportsBgra8Uav()) {
        throw UnsupportedFeatureError("D3D12Remapper::CreateOutputTexture: B8G8R8A8 UAV typed store is not supported");
    }
    return CreateTexture2D(core, width, height, format, initialState, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

} // namespace Processing
} // namespace D3D12CoreLib
