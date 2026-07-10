//
// test_CompatibilityV1121ThresholdPyramid.cpp
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Processing/D3D12ThresholdProcessor.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidProcessor.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidBlur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12PyramidRegionBlur.hpp>

#include <type_traits>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

template <class Desc>
using ThresholdLikeSignature = void (D3D12ThresholdProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const Desc&,
    const D3D12ProcessingStateDesc&);

using RecordDownsampleSignature = void (D3D12PyramidProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const PyramidDownsampleDesc&,
    const D3D12ProcessingStateDesc&);

using RecordUpsampleSignature = void (D3D12PyramidProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const PyramidUpsampleDesc&,
    const D3D12ProcessingStateDesc&);

using RecordPyramidBlurSignature = void (D3D12PyramidBlur::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12PyramidBlurWorkspace&,
    D3D12Resource&,
    const PyramidBlurDesc&);

using RecordPyramidRegionBlurSignature = void (D3D12PyramidRegionBlur::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12PyramidRegionBlurWorkspace&,
    D3D12Resource&,
    const PyramidRegionBlurDesc&);

static_assert(std::is_same_v<
    decltype(&D3D12ThresholdProcessor::RecordThreshold),
    ThresholdLikeSignature<ThresholdDesc>>);
static_assert(std::is_same_v<
    decltype(&D3D12ThresholdProcessor::RecordRangeThreshold),
    ThresholdLikeSignature<RangeThresholdDesc>>);
static_assert(std::is_same_v<
    decltype(&D3D12ThresholdProcessor::RecordConfidenceHeatmap),
    ThresholdLikeSignature<ConfidenceHeatmapDesc>>);
static_assert(std::is_same_v<
    decltype(&D3D12ThresholdProcessor::RecordClassColorMap),
    ThresholdLikeSignature<ClassColorMapDesc>>);
static_assert(std::is_same_v<
    decltype(&D3D12ThresholdProcessor::RecordMaskOverlay),
    ThresholdLikeSignature<MaskOverlayDesc>>);
static_assert(std::is_same_v<
    decltype(&D3D12PyramidProcessor::RecordDownsample2x),
    RecordDownsampleSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12PyramidProcessor::RecordUpsample2x),
    RecordUpsampleSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12PyramidBlur::RecordPyramidBlur),
    RecordPyramidBlurSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12PyramidRegionBlur::RecordPyramidRegionBlur),
    RecordPyramidRegionBlurSignature>);

} // namespace

TEST(CompatibilityV1121, ThresholdAndPyramidSignaturesCompile) {
    ThresholdLikeSignature<ThresholdDesc> threshold =
        &D3D12ThresholdProcessor::RecordThreshold;
    ThresholdLikeSignature<RangeThresholdDesc> rangeThreshold =
        &D3D12ThresholdProcessor::RecordRangeThreshold;
    RecordDownsampleSignature downsample =
        &D3D12PyramidProcessor::RecordDownsample2x;
    RecordPyramidBlurSignature pyramidBlur =
        &D3D12PyramidBlur::RecordPyramidBlur;
    RecordPyramidRegionBlurSignature pyramidRegionBlur =
        &D3D12PyramidRegionBlur::RecordPyramidRegionBlur;

    CHECK(threshold != nullptr);
    CHECK(rangeThreshold != nullptr);
    CHECK(downsample != nullptr);
    CHECK(pyramidBlur != nullptr);
    CHECK(pyramidRegionBlur != nullptr);
}
