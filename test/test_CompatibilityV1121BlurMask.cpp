//
// test_CompatibilityV1121BlurMask.cpp
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Processing/D3D12Blur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12RegionBlur.hpp>
#include <D3D12Helper/D3D12Processing/D3D12MaskProcessor.hpp>

#include <type_traits>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

using RecordBlurSignature = void (D3D12Blurrer::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const BlurDesc&,
    const D3D12ProcessingBlurStateDesc&);

using RecordRegionBlurSignature = void (D3D12RegionBlur::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const RegionBlurDesc&,
    const D3D12ProcessingRegionBlurStateDesc&);

using RecordApplyMaskSignature = void (D3D12MaskProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const MaskApplyDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

using RecordBlendByMaskSignature = void (D3D12MaskProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const MaskBlendDesc&,
    const D3D12ProcessingThreeInputStateDesc&);

using RecordCombineMasksSignature = void (D3D12MaskProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const MaskCombineDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

using RecordInvertMaskSignature = void (D3D12MaskProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const MaskInvertDesc&,
    const D3D12ProcessingStateDesc&);

static_assert(std::is_same_v<decltype(&D3D12Blurrer::RecordBlur), RecordBlurSignature>);
static_assert(std::is_same_v<decltype(&D3D12RegionBlur::RecordRegionBlur), RecordRegionBlurSignature>);
static_assert(std::is_same_v<decltype(&D3D12MaskProcessor::RecordApplyMask), RecordApplyMaskSignature>);
static_assert(std::is_same_v<decltype(&D3D12MaskProcessor::RecordBlendByMask), RecordBlendByMaskSignature>);
static_assert(std::is_same_v<decltype(&D3D12MaskProcessor::RecordCombineMasks), RecordCombineMasksSignature>);
static_assert(std::is_same_v<decltype(&D3D12MaskProcessor::RecordInvertMask), RecordInvertMaskSignature>);

} // namespace

TEST(CompatibilityV1121, BlurRegionBlurAndMaskSignaturesCompile) {
    RecordBlurSignature blur = &D3D12Blurrer::RecordBlur;
    RecordRegionBlurSignature regionBlur = &D3D12RegionBlur::RecordRegionBlur;
    RecordApplyMaskSignature apply = &D3D12MaskProcessor::RecordApplyMask;
    RecordBlendByMaskSignature blend = &D3D12MaskProcessor::RecordBlendByMask;
    RecordCombineMasksSignature combine = &D3D12MaskProcessor::RecordCombineMasks;
    RecordInvertMaskSignature invert = &D3D12MaskProcessor::RecordInvertMask;

    CHECK(blur != nullptr);
    CHECK(regionBlur != nullptr);
    CHECK(apply != nullptr);
    CHECK(blend != nullptr);
    CHECK(combine != nullptr);
    CHECK(invert != nullptr);
}
