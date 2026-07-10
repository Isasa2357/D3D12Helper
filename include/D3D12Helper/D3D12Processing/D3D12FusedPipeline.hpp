#pragma once
//
// D3D12FusedPipeline.hpp
// One-dispatch fused Processing passes.
//
#include <D3D12Helper/D3D12Processing/D3D12ProcessingContext.hpp>
#include <D3D12Helper/D3D12Processing/D3D12ProcessingShaderCache.hpp>
#include <D3D12Helper/D3D12Processing/D3D12TextureViews.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Framework/D3D12ComputePipeline.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceView.hpp>

#include <memory>

namespace D3D12CoreLib {
namespace Processing {

class D3D12FusedProcessor {
public:
    D3D12FusedProcessor();
    ~D3D12FusedProcessor();

    D3D12FusedProcessor(const D3D12FusedProcessor&) = delete;
    D3D12FusedProcessor& operator=(const D3D12FusedProcessor&) = delete;
    D3D12FusedProcessor(D3D12FusedProcessor&&) noexcept;
    D3D12FusedProcessor& operator=(D3D12FusedProcessor&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    // format conversion と resize を 1 dispatch で行う。
    // 対応: RGBA-like -> RGBA-like, YUV420(NV12/P010) -> RGBA-like。
    void RecordConvertResize(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const FusedConvertResizeDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    // Non-owning counterpart. state.useExplicitStates must be true and both
    // resource owners must keep their resources alive through GPU completion.
    void RecordConvertResizeView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const FusedConvertResizeDesc& desc,
        const D3D12ProcessingStateDesc& state);

    D3D12Resource CreateOutputTexture(
        D3D12Core& core,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

private:
    struct Pipelines;

    void EnsureInitialized() const;
    void EnsurePipelines();

    void RecordConvertResizeImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FusedConvertResizeDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    void RecordRgbToRgbResizeImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FusedConvertResizeDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    void RecordYuv420ToRgbResizeImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FusedConvertResizeDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    D3D12ProcessingContext* m_context = nullptr;
    D3D12ProcessingShaderCache m_shaderCache;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
