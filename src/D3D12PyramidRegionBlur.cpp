#include <D3D12Helper/D3D12Processing/D3D12PyramidRegionBlur.hpp>

#include <D3D12Helper/D3D12Core/D3D12Barrier.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <cmath>
#include <memory>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

constexpr UINT kThreadGroupX = 16;
constexpr UINT kThreadGroupY = 16;

struct D3D12RegionBlurBlendConstants {
    UINT srcWidth = 0;
    UINT srcHeight = 0;
    UINT dstWidth = 0;
    UINT dstHeight = 0;

    INT srcX = 0;
    INT srcY = 0;
    INT dstX = 0;
    INT dstY = 0;

    UINT shape = 0;
    UINT selection = 0;
    UINT reserved0 = 0;
    UINT reserved1 = 0;

    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float edgeSoftness = 0.0f;

    float rectX = 0.0f;
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;

    float blurStrength = 1.0f;
    float reserved2 = 0.0f;
    float reserved3 = 0.0f;
    float reserved4 = 0.0f;
};
static_assert((sizeof(D3D12RegionBlurBlendConstants) % 4) == 0, "root constants must be DWORD aligned");

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

void ValidateRange01(float v, const char* functionName, const char* fieldName) {
    ValidateFinite(v, functionName, fieldName);
    if (v < 0.0f || v > 1.0f) {
        std::ostringstream os;
        os << functionName << ": " << fieldName << " must be in [0, 1]";
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
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Width == 0 || desc.Height == 0) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is not a valid Texture2D";
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

void ValidatePyramidRegionBlurDesc(const PyramidRegionBlurDesc& desc, const char* functionName) {
    ValidateFinite(desc.centerX, functionName, "centerX");
    ValidateFinite(desc.centerY, functionName, "centerY");
    ValidateFinite(desc.radius, functionName, "radius");
    ValidateFinite(desc.rectX, functionName, "rectX");
    ValidateFinite(desc.rectY, functionName, "rectY");
    ValidateFinite(desc.rectWidth, functionName, "rectWidth");
    ValidateFinite(desc.rectHeight, functionName, "rectHeight");
    ValidateFinite(desc.edgeSoftness, functionName, "edgeSoftness");
    ValidateRange01(desc.blurStrength, functionName, "blurStrength");

    if (desc.edgeSoftness < 0.0f) {
        throw ValidationError(std::string(functionName) + ": edgeSoftness must be >= 0");
    }

    switch (desc.shape) {
    case RegionShape::Circle:
        if (!(desc.radius > 0.0f)) {
            throw ValidationError(std::string(functionName) + ": radius must be > 0 for circle region");
        }
        break;
    case RegionShape::Rect:
        if (!(desc.rectWidth > 0.0f) || !(desc.rectHeight > 0.0f)) {
            throw ValidationError(std::string(functionName) + ": rectWidth and rectHeight must be > 0 for rect region");
        }
        break;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported region shape");
    }

    switch (desc.selection) {
    case RegionSelection::Inside:
    case RegionSelection::Outside:
        break;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported region selection");
    }
}

D3D12RegionBlurBlendConstants MakeBlendConstants(
    const ProcessingRect& srcRect,
    const ProcessingRect& dstRect,
    const PyramidRegionBlurDesc& desc) {

    D3D12RegionBlurBlendConstants c = {};
    c.srcWidth = srcRect.width;
    c.srcHeight = srcRect.height;
    c.dstWidth = dstRect.width;
    c.dstHeight = dstRect.height;
    c.srcX = srcRect.x;
    c.srcY = srcRect.y;
    c.dstX = dstRect.x;
    c.dstY = dstRect.y;
    c.shape = static_cast<UINT>(desc.shape);
    c.selection = static_cast<UINT>(desc.selection);
    c.centerX = desc.centerX;
    c.centerY = desc.centerY;
    c.radius = desc.radius;
    c.edgeSoftness = desc.edgeSoftness;
    c.rectX = desc.rectX;
    c.rectY = desc.rectY;
    c.rectWidth = desc.rectWidth;
    c.rectHeight = desc.rectHeight;
    c.blurStrength = desc.blurStrength;
    return c;
}

void TransitionIfNeeded(
    D3D12CommandContext& commandContext,
    D3D12Resource& resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {

    if (before == after) {
        resource.SetState(after);
        return;
    }

    commandContext.ResourceBarrier(MakeTransitionBarrier(resource.Get(), before, after));
    resource.SetState(after);
}

void SetDescriptorHeap(D3D12ProcessingContext& context, D3D12CommandContext& commandContext) {
    auto* cmd = commandContext.GetCommandList();
    if (!cmd) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: command context has no command list");
    }

    ID3D12DescriptorHeap* heaps[] = { context.CbvSrvUavAllocator().GetHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
}

void SetRootConstants(
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12RegionBlurBlendConstants& constants) {

    const UINT index = pipeline.RootConstantsIndex();
    if (index == UINT_MAX) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: pipeline has no root constants slot");
    }

    commandContext.GetCommandList()->SetComputeRoot32BitConstants(
        index,
        static_cast<UINT>(sizeof(D3D12RegionBlurBlendConstants) / 4),
        &constants,
        0);
}

D3D12DescriptorRange CreateTwoRgbaSrvRange(
    D3D12ProcessingContext& context,
    const D3D12Resource& original,
    DXGI_FORMAT originalFormat,
    const D3D12Resource& blurred,
    DXGI_FORMAT blurredFormat) {

    auto range = context.CbvSrvUavAllocator().AllocateRange(2);
    if (!range.shaderVisible) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: descriptor range must be shader-visible");
    }

    CreateTexture2DSrv(context.Core(), original, range.Cpu(0), originalFormat);
    CreateTexture2DSrv(context.Core(), blurred, range.Cpu(1), blurredFormat);
    return range;
}

void BindAndDispatchBlend(
    D3D12ProcessingContext& context,
    const D3D12ComputePipeline& pipeline,
    D3D12CommandContext& commandContext,
    const D3D12DescriptorRange& srvRange,
    const D3D12TextureViewSet& dstViews,
    UINT dstUavIndex,
    const D3D12RegionBlurBlendConstants& constants) {

    SetDescriptorHeap(context, commandContext);
    pipeline.Bind(commandContext);
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(pipeline.SrvTableIndex(), srvRange.Gpu(0));
    commandContext.GetCommandList()->SetComputeRootDescriptorTable(pipeline.UavTableIndex(), dstViews.Gpu(dstUavIndex));
    SetRootConstants(pipeline, commandContext, constants);
    pipeline.Dispatch(
        commandContext,
        DivideRoundUp(constants.dstWidth, kThreadGroupX),
        DivideRoundUp(constants.dstHeight, kThreadGroupY),
        1);
}

} // namespace

struct D3D12PyramidRegionBlur::Pipelines {
    D3D12ComputePipeline blend;
    bool initialized = false;
};

D3D12PyramidRegionBlur::D3D12PyramidRegionBlur() = default;
D3D12PyramidRegionBlur::~D3D12PyramidRegionBlur() = default;
D3D12PyramidRegionBlur::D3D12PyramidRegionBlur(D3D12PyramidRegionBlur&&) noexcept = default;
D3D12PyramidRegionBlur& D3D12PyramidRegionBlur::operator=(D3D12PyramidRegionBlur&&) noexcept = default;

void D3D12PyramidRegionBlur::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_shaderCache.Initialize(context);
    m_pyramidBlur.Initialize(context);
    m_pipelines.reset();
}

void D3D12PyramidRegionBlur::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12PyramidRegionBlur: processor is not initialized");
    }
}

void D3D12PyramidRegionBlur::EnsurePipelines() {
    EnsureInitialized();

    if (!m_pipelines) {
        m_pipelines.reset(new Pipelines());
    }
    if (m_pipelines->initialized) {
        return;
    }

    ComputePipelineDesc desc = {};
    desc.numSrvs = 2;
    desc.numUavs = 1;
    desc.numRootConstantValues = static_cast<UINT>(sizeof(D3D12RegionBlurBlendConstants) / 4);

    m_pipelines->blend.InitializeWithTemplate(
        m_context->GetDevice(),
        m_shaderCache.GetComputeShader("RegionBlurBlendRgba.hlsl"),
        desc);
    m_pipelines->initialized = true;
}

D3D12PyramidRegionBlurWorkspace D3D12PyramidRegionBlur::CreateWorkspace(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT levels,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();

    D3D12PyramidRegionBlurWorkspace workspace;
    workspace.sourceWidth = width;
    workspace.sourceHeight = height;
    workspace.levels = levels;
    workspace.format = format;
    workspace.blurWorkspace = m_pyramidBlur.CreateWorkspace(core, width, height, format, levels, initialState);
    workspace.blurred = m_pyramidBlur.CreateOutputTexture(core, width, height, format, initialState);
    return workspace;
}

D3D12Resource D3D12PyramidRegionBlur::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    return m_pyramidBlur.CreateOutputTexture(core, width, height, format, initialState);
}

void D3D12PyramidRegionBlur::RecordPyramidRegionBlur(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12PyramidRegionBlurWorkspace& workspace,
    D3D12Resource& dst,
    const PyramidRegionBlurDesc& desc) {

    EnsurePipelines();
    constexpr const char* kFunction = "D3D12PyramidRegionBlur::RecordPyramidRegionBlur";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(workspace.blurred, kFunction, "workspace.blurred");
    ValidateTexture2D(dst, kFunction, "dst");

    ValidateNotSameResource(src, workspace.blurred, kFunction);
    ValidateNotSameResource(src, dst, kFunction);
    ValidateNotSameResource(workspace.blurred, dst, kFunction);
    ValidateOutputUav(workspace.blurred, kFunction, "workspace.blurred");
    ValidateOutputUav(dst, kFunction, "dst");

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat) || !IsRgbaLikeFormat(workspace.blurred.GetFormat())) {
        throw UnsupportedFormatError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: only RGBA-like formats are supported");
    }
    ValidateRgbaUavSupport(*m_context, workspace.blurred.GetFormat(), kFunction);
    ValidateRgbaUavSupport(*m_context, dstFormat, kFunction);

    const auto srcDesc = src.GetDesc();
    const auto dstDesc = dst.GetDesc();
    const ProcessingRect srcRect = ResolveRect(desc.srcRect, static_cast<UINT>(srcDesc.Width), srcDesc.Height);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, static_cast<UINT>(dstDesc.Width), dstDesc.Height);

    ValidateRectInside(srcRect, static_cast<UINT>(srcDesc.Width), srcDesc.Height, kFunction, "srcRect");
    ValidateRectInside(dstRect, static_cast<UINT>(dstDesc.Width), dstDesc.Height, kFunction, "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: processing does not resize; srcRect and dstRect sizes must match");
    }

    if (workspace.sourceWidth != srcRect.width || workspace.sourceHeight != srcRect.height || workspace.levels != desc.levels) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: workspace does not match desc size/levels");
    }
    if (workspace.blurred.GetDesc().Width < dstRect.width || workspace.blurred.GetDesc().Height < dstRect.height) {
        throw ValidationError("D3D12PyramidRegionBlur::RecordPyramidRegionBlur: workspace.blurred is smaller than processing rect");
    }

    ValidatePyramidRegionBlurDesc(desc, kFunction);

    PyramidBlurDesc blurDesc = {};
    blurDesc.srcFormat = srcFormat;
    blurDesc.dstFormat = workspace.blurred.GetFormat();
    blurDesc.srcRect = srcRect;
    blurDesc.dstRect = { 0, 0, dstRect.width, dstRect.height };
    blurDesc.levels = desc.levels;
    blurDesc.blurMode = desc.blurMode;
    blurDesc.blurRadius = desc.blurRadius;
    blurDesc.blurSigma = desc.blurSigma;
    blurDesc.blurEdgeMode = desc.blurEdgeMode;
    std::copy(desc.blurBorderColor, desc.blurBorderColor + 4, blurDesc.blurBorderColor);
    blurDesc.upsampleFilter = desc.upsampleFilter;
    blurDesc.pyramidEdgeMode = desc.pyramidEdgeMode;
    std::copy(desc.pyramidBorderColor, desc.pyramidBorderColor + 4, blurDesc.pyramidBorderColor);

    m_pyramidBlur.RecordPyramidBlur(commandContext, src, workspace.blurWorkspace, workspace.blurred, blurDesc);

    TransitionIfNeeded(commandContext, src, src.GetState(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandContext, workspace.blurred, workspace.blurred.GetState(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandContext, dst, dst.GetState(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto srvRange = CreateTwoRgbaSrvRange(*m_context, src, srcFormat, workspace.blurred, workspace.blurred.GetFormat());
    auto dstView = CreateRgbaTextureViewSet(*m_context, dst, false, true, dstFormat);

    const auto constants = MakeBlendConstants(srcRect, dstRect, desc);
    BindAndDispatchBlend(
        *m_context,
        m_pipelines->blend,
        commandContext,
        srvRange,
        dstView,
        dstView.uavIndex,
        constants);

    commandContext.ResourceBarrier(MakeUavBarrier(dst.Get()));
}

} // namespace Processing
} // namespace D3D12CoreLib
