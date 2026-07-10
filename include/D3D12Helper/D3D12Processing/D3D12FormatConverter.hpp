#pragma once
//
// D3D12FormatConverter.hpp
// GPU format conversion passes for D3D12 resources.
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

class D3D12FormatConverter {
public:
    D3D12FormatConverter();
    ~D3D12FormatConverter();

    D3D12FormatConverter(const D3D12FormatConverter&) = delete;
    D3D12FormatConverter& operator=(const D3D12FormatConverter&) = delete;
    D3D12FormatConverter(D3D12FormatConverter&&) noexcept;
    D3D12FormatConverter& operator=(D3D12FormatConverter&&) noexcept;

    void Initialize(D3D12ProcessingContext& context);

    // Existing owned-resource API. When useExplicitStates is false, the
    // D3D12Resource single-state tracker is used exactly as before.
    void RecordConvert(
        D3D12CommandContext& commandContext,
        D3D12Resource& src,
        D3D12Resource& dst,
        const FormatConvertDesc& desc,
        const D3D12ProcessingStateDesc& state = {});

    // Non-owning API for resources supplied by a camera SDK, runtime, swapchain,
    // or another subsystem. state.useExplicitStates must be true because a
    // D3D12ResourceView intentionally has no state tracker. The resource owners
    // must keep both resources alive until the submitted GPU work completes.
    void RecordConvertView(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        const FormatConvertDesc& desc,
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

    void RecordConvertImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FormatConvertDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    void RecordRgbToRgbImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FormatConvertDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    void RecordYuv420ToRgbImpl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FormatConvertDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    void RecordRgbToYuv420Impl(
        D3D12CommandContext& commandContext,
        D3D12ResourceView src,
        D3D12ResourceView dst,
        D3D12Resource* trackedSrc,
        D3D12Resource* trackedDst,
        const FormatConvertDesc& desc,
        const D3D12ProcessingStateDesc& state,
        const char* functionName);

    D3D12ProcessingContext* m_context = nullptr;
    D3D12ProcessingShaderCache m_shaderCache;
    std::unique_ptr<Pipelines> m_pipelines;
};

} // namespace Processing
} // namespace D3D12CoreLib
