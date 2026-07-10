//
// test_CompatibilityV1121ProcessingExpansion.cpp
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Processing/D3D12Resize.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Remap.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Composite.hpp>

#include <type_traits>

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

using RecordResizeSignature = void (D3D12Resizer::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    const ResizeDesc&,
    const D3D12ProcessingStateDesc&);

using RecordRemapSignature = void (D3D12Remapper::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const RemapDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

using RecordCompositeSignature = void (D3D12Compositor::*)(
    D3D12CommandContext&,
    D3D12Resource&,
    D3D12Resource&,
    D3D12Resource&,
    const CompositeDesc&,
    const D3D12ProcessingTwoInputStateDesc&);

static_assert(std::is_same_v<
    decltype(&D3D12Resizer::RecordResize),
    RecordResizeSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12Remapper::RecordRemap),
    RecordRemapSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12Compositor::RecordComposite),
    RecordCompositeSignature>);

} // namespace

TEST(CompatibilityV1121, ProcessingExpansionSignaturesCompile) {
    RecordResizeSignature resize = &D3D12Resizer::RecordResize;
    RecordRemapSignature remap = &D3D12Remapper::RecordRemap;
    RecordCompositeSignature composite = &D3D12Compositor::RecordComposite;

    CHECK(resize != nullptr);
    CHECK(remap != nullptr);
    CHECK(composite != nullptr);
}
