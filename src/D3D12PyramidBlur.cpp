#include <D3D12Helper/D3D12Processing/D3D12PyramidBlur.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Helpers.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace D3D12CoreLib {
namespace Processing {
namespace {

UINT HalfRoundUp(UINT value) noexcept {
    return value == 0 ? 0 : ((value + 1u) / 2u);
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

void ValidateRgbaFormats(D3D12ProcessingContext& context, DXGI_FORMAT srcFormat, DXGI_FORMAT dstFormat, const char* functionName) {
    if (!IsRgbaLikeFormat(srcFormat) || !IsRgbaLikeFormat(dstFormat)) {
        throw UnsupportedFormatError(std::string(functionName) + ": only RGBA-like formats are supported");
    }
    ValidateRgbaUavSupport(context, dstFormat, functionName);
}

void ValidateLevels(UINT levels, const char* functionName) {
    if (levels == 0 || levels > D3D12PyramidBlur::MaxLevels) {
        std::ostringstream os;
        os << functionName << ": levels must be in [1, " << D3D12PyramidBlur::MaxLevels << "]";
        throw ValidationError(os.str());
    }
}

void ValidateDivisibleByPyramidLevels(UINT width, UINT height, UINT levels, const char* functionName) {
    const UINT divisor = 1u << levels;
    if ((width % divisor) != 0 || (height % divisor) != 0) {
        std::ostringstream os;
        os << functionName << ": processing rect size must be divisible by 2^levels; size="
           << width << "x" << height << " levels=" << levels;
        throw ValidationError(os.str());
    }
}

void ValidateProcessingFilter(ProcessingFilter filter, const char* functionName) {
    switch (filter) {
    case ProcessingFilter::Point:
    case ProcessingFilter::Linear:
        return;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported upsample filter");
    }
}

void ValidatePyramidEdgeMode(PyramidEdgeMode mode, const char* functionName) {
    switch (mode) {
    case PyramidEdgeMode::Clamp:
    case PyramidEdgeMode::Constant:
        return;
    default:
        throw ValidationError(std::string(functionName) + ": unsupported pyramid edge mode");
    }
}

void ValidateWorkspaceResource(
    const D3D12Resource& resource,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    const char* functionName,
    const char* argumentName) {

    ValidateTexture2D(resource, functionName, argumentName);
    ValidateOutputUav(resource, functionName, argumentName);

    const auto desc = resource.GetDesc();
    if (desc.Width < width || desc.Height < height) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " is too small; expected at least "
           << width << "x" << height << " actual "
           << static_cast<UINT>(desc.Width) << "x" << desc.Height;
        throw ValidationError(os.str());
    }
    if (resource.GetFormat() != format) {
        std::ostringstream os;
        os << functionName << ": " << argumentName << " format does not match workspace format";
        throw ValidationError(os.str());
    }
}

} // namespace

D3D12PyramidBlur::D3D12PyramidBlur() = default;
D3D12PyramidBlur::~D3D12PyramidBlur() = default;
D3D12PyramidBlur::D3D12PyramidBlur(D3D12PyramidBlur&&) noexcept = default;
D3D12PyramidBlur& D3D12PyramidBlur::operator=(D3D12PyramidBlur&&) noexcept = default;

void D3D12PyramidBlur::Initialize(D3D12ProcessingContext& context) {
    m_context = &context;
    m_pyramid.Initialize(context);
    m_blurrer.Initialize(context);
}

void D3D12PyramidBlur::EnsureInitialized() const {
    if (!m_context) {
        throw ValidationError("D3D12PyramidBlur: processor is not initialized");
    }
}

D3D12PyramidBlurWorkspace D3D12PyramidBlur::CreateWorkspace(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT levels,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    constexpr const char* kFunction = "D3D12PyramidBlur::CreateWorkspace";

    if (width == 0 || height == 0) {
        throw ValidationError("D3D12PyramidBlur::CreateWorkspace: size is zero");
    }
    if (!IsRgbaLikeFormat(format)) {
        throw UnsupportedFormatError("D3D12PyramidBlur::CreateWorkspace: only RGBA-like formats are supported");
    }

    ValidateLevels(levels, kFunction);
    ValidateDivisibleByPyramidLevels(width, height, levels, kFunction);
    ValidateRgbaUavSupport(*m_context, format, kFunction);

    D3D12PyramidBlurWorkspace workspace;
    workspace.sourceWidth = width;
    workspace.sourceHeight = height;
    workspace.levels = levels;
    workspace.format = format;

    UINT w = width;
    UINT h = height;
    workspace.downTextures.reserve(levels);
    for (UINT i = 0; i < levels; ++i) {
        w = HalfRoundUp(w);
        h = HalfRoundUp(h);
        workspace.downTextures.emplace_back(m_pyramid.CreateOutputTexture(core, w, h, format, initialState));
    }

    workspace.blurScratch = m_blurrer.CreateScratchTexture(core, w, h, format, initialState);
    workspace.blurredLow = m_blurrer.CreateOutputTexture(core, w, h, format, initialState);

    if (levels > 1) {
        workspace.upTextures.reserve(levels - 1u);
        for (UINT i = 0; i < levels - 1u; ++i) {
            const auto desc = workspace.downTextures[i].GetDesc();
            workspace.upTextures.emplace_back(
                m_pyramid.CreateOutputTexture(core, static_cast<UINT>(desc.Width), desc.Height, format, initialState));
        }
    }

    return workspace;
}

D3D12Resource D3D12PyramidBlur::CreateOutputTexture(
    D3D12Core& core,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    D3D12_RESOURCE_STATES initialState) {

    EnsureInitialized();
    return m_pyramid.CreateOutputTexture(core, width, height, format, initialState);
}

void D3D12PyramidBlur::RecordPyramidBlur(
    D3D12CommandContext& commandContext,
    D3D12Resource& src,
    D3D12PyramidBlurWorkspace& workspace,
    D3D12Resource& dst,
    const PyramidBlurDesc& desc) {

    EnsureInitialized();
    constexpr const char* kFunction = "D3D12PyramidBlur::RecordPyramidBlur";

    ValidateTexture2D(src, kFunction, "src");
    ValidateTexture2D(dst, kFunction, "dst");
    ValidateNotSameResource(src, dst, kFunction);
    ValidateOutputUav(dst, kFunction, "dst");

    ValidateLevels(desc.levels, kFunction);
    ValidateProcessingFilter(desc.upsampleFilter, kFunction);
    ValidatePyramidEdgeMode(desc.pyramidEdgeMode, kFunction);

    const DXGI_FORMAT srcFormat = ResolveFormat(desc.srcFormat, src);
    const DXGI_FORMAT dstFormat = ResolveFormat(desc.dstFormat, dst);
    ValidateRgbaFormats(*m_context, srcFormat, dstFormat, kFunction);

    if (workspace.levels != desc.levels) {
        throw ValidationError("D3D12PyramidBlur::RecordPyramidBlur: workspace levels do not match desc.levels");
    }
    if (workspace.format != dstFormat) {
        throw ValidationError("D3D12PyramidBlur::RecordPyramidBlur: workspace format does not match dst format");
    }
    if (workspace.downTextures.size() != desc.levels || workspace.upTextures.size() != (desc.levels - 1u)) {
        throw ValidationError("D3D12PyramidBlur::RecordPyramidBlur: invalid workspace texture count");
    }

    const auto srcTexDesc = src.GetDesc();
    const auto dstTexDesc = dst.GetDesc();
    const ProcessingRect srcRect = ResolveRect(desc.srcRect, static_cast<UINT>(srcTexDesc.Width), srcTexDesc.Height);
    const ProcessingRect dstRect = ResolveRect(desc.dstRect, static_cast<UINT>(dstTexDesc.Width), dstTexDesc.Height);

    ValidateRectInside(srcRect, static_cast<UINT>(srcTexDesc.Width), srcTexDesc.Height, kFunction, "srcRect");
    ValidateRectInside(dstRect, static_cast<UINT>(dstTexDesc.Width), dstTexDesc.Height, kFunction, "dstRect");

    if (srcRect.width != dstRect.width || srcRect.height != dstRect.height) {
        throw ValidationError("D3D12PyramidBlur::RecordPyramidBlur: pyramid blur does not resize; srcRect and dstRect sizes must match");
    }

    ValidateDivisibleByPyramidLevels(srcRect.width, srcRect.height, desc.levels, kFunction);

    if (workspace.sourceWidth != srcRect.width || workspace.sourceHeight != srcRect.height) {
        throw ValidationError("D3D12PyramidBlur::RecordPyramidBlur: workspace size does not match processing rect size");
    }

    D3D12Resource* current = &src;
    ProcessingRect currentRect = srcRect;

    for (UINT i = 0; i < desc.levels; ++i) {
        const UINT nextW = currentRect.width / 2u;
        const UINT nextH = currentRect.height / 2u;
        ValidateWorkspaceResource(workspace.downTextures[i], nextW, nextH, dstFormat, kFunction, "downTextures");

        PyramidDownsampleDesc down = {};
        down.srcRect = currentRect;
        down.dstRect = { 0, 0, nextW, nextH };
        down.edgeMode = desc.pyramidEdgeMode;
        std::copy(desc.pyramidBorderColor, desc.pyramidBorderColor + 4, down.borderColor);

        m_pyramid.RecordDownsample2x(commandContext, *current, workspace.downTextures[i], down);
        current = &workspace.downTextures[i];
        currentRect = { 0, 0, nextW, nextH };
    }

    ValidateWorkspaceResource(workspace.blurScratch, currentRect.width, currentRect.height, dstFormat, kFunction, "blurScratch");
    ValidateWorkspaceResource(workspace.blurredLow, currentRect.width, currentRect.height, dstFormat, kFunction, "blurredLow");

    BlurDesc blur = {};
    blur.mode = desc.blurMode;
    blur.srcRect = currentRect;
    blur.dstRect = currentRect;
    blur.radius = desc.blurRadius;
    blur.sigma = desc.blurSigma;
    blur.edgeMode = desc.blurEdgeMode;
    std::copy(desc.blurBorderColor, desc.blurBorderColor + 4, blur.borderColor);

    m_blurrer.RecordBlur(commandContext, *current, workspace.blurScratch, workspace.blurredLow, blur);
    current = &workspace.blurredLow;

    for (int level = static_cast<int>(desc.levels) - 1; level >= 0; --level) {
        const UINT nextW = currentRect.width * 2u;
        const UINT nextH = currentRect.height * 2u;

        D3D12Resource* target = nullptr;
        ProcessingRect targetRect = {};
        if (level == 0) {
            target = &dst;
            targetRect = dstRect;
        } else {
            target = &workspace.upTextures[static_cast<size_t>(level - 1)];
            targetRect = { 0, 0, nextW, nextH };
            ValidateWorkspaceResource(*target, nextW, nextH, dstFormat, kFunction, "upTextures");
        }

        PyramidUpsampleDesc up = {};
        up.srcRect = currentRect;
        up.dstRect = targetRect;
        up.filter = desc.upsampleFilter;
        up.edgeMode = desc.pyramidEdgeMode;
        std::copy(desc.pyramidBorderColor, desc.pyramidBorderColor + 4, up.borderColor);

        m_pyramid.RecordUpsample2x(commandContext, *current, *target, up);
        current = target;
        currentRect = { 0, 0, nextW, nextH };
    }
}

} // namespace Processing
} // namespace D3D12CoreLib
