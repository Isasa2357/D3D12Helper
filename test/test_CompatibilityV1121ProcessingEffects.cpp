//
// test_CompatibilityV1121ProcessingEffects.cpp
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Processing/D3D12ColorAdjust.hpp>
#include <D3D12Helper/D3D12Processing/D3D12KernelFilter.hpp>
#include <D3D12Helper/D3D12Processing/D3D12RegionEffect.hpp>

#include <type_traits>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

using RecordColorAdjustSignature = void (D3D12ColorAdjuster::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const ColorAdjustDesc&,
    const D3D12ProcessingStateDesc&);

using RecordKernelFilterSignature = void (D3D12KernelFilter::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const KernelFilterDesc&,
    const D3D12ProcessingStateDesc&);

using RecordRegionEffectSignature = void (D3D12RegionEffectProcessor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const RegionEffectDesc&,
    const D3D12ProcessingStateDesc&);

static_assert(std::is_same_v<
    decltype(&D3D12ColorAdjuster::RecordColorAdjust),
    RecordColorAdjustSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12KernelFilter::RecordKernelFilter),
    RecordKernelFilterSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12RegionEffectProcessor::RecordRegionEffect),
    RecordRegionEffectSignature>);

} // namespace

TEST(CompatibilityV1121, ProcessingEffectsSignaturesCompile) {
    RecordColorAdjustSignature colorAdjust = &D3D12ColorAdjuster::RecordColorAdjust;
    RecordKernelFilterSignature kernelFilter = &D3D12KernelFilter::RecordKernelFilter;
    RecordRegionEffectSignature regionEffect = &D3D12RegionEffectProcessor::RecordRegionEffect;

    CHECK(colorAdjust != nullptr);
    CHECK(kernelFilter != nullptr);
    CHECK(regionEffect != nullptr);
}
