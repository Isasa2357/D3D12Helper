//
// test_CompatibilityV1121Advanced.cpp
// Existing AdvancedProcessing member signatures must remain unambiguous.
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Processing/D3D12AdvancedProcessing.hpp>

#include <type_traits>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

using RecordAffineTransformSignature = void (D3D12AdvancedProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const AffineTransformDesc&,
    const D3D12ProcessingStateDesc&);

using RecordPerspectiveTransformSignature = void (D3D12AdvancedProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const PerspectiveTransformDesc&,
    const D3D12ProcessingStateDesc&);

using RecordApplyLut3DSignature = void (D3D12AdvancedProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const Lut3DDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

using RecordApplyUndistortMapSignature = void (D3D12AdvancedProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const RemapDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

static_assert(std::is_same_v<
    decltype(&D3D12AdvancedProcessor::RecordAffineTransform),
    RecordAffineTransformSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12AdvancedProcessor::RecordPerspectiveTransform),
    RecordPerspectiveTransformSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12AdvancedProcessor::RecordApplyLut3D),
    RecordApplyLut3DSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12AdvancedProcessor::RecordApplyUndistortMap),
    RecordApplyUndistortMapSignature>);

} // namespace

TEST(CompatibilityV1121, AdvancedProcessingSignaturesCompile) {
    RecordAffineTransformSignature affine =
        &D3D12AdvancedProcessor::RecordAffineTransform;
    RecordPerspectiveTransformSignature perspective =
        &D3D12AdvancedProcessor::RecordPerspectiveTransform;
    RecordApplyLut3DSignature lut =
        &D3D12AdvancedProcessor::RecordApplyLut3D;
    RecordApplyUndistortMapSignature undistort =
        &D3D12AdvancedProcessor::RecordApplyUndistortMap;

    CHECK(affine != nullptr);
    CHECK(perspective != nullptr);
    CHECK(lut != nullptr);
    CHECK(undistort != nullptr);
}
