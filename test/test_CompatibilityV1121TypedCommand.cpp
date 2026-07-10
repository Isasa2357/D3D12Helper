//
// test_CompatibilityV1121TypedCommand.cpp
// Existing D3D12CommandContext signatures must remain unique and unchanged.
//
#include "TestFramework.hpp"

#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>

#include <type_traits>

using namespace D3D12CoreLib;

namespace {

using InitializeSignature = void (D3D12CommandContext::*)(
    ID3D12Device*,
    D3D12_COMMAND_LIST_TYPE);
using ResetSignature = void (D3D12CommandContext::*)();
using CloseSignature = void (D3D12CommandContext::*)();
using GetCommandListSignature = ID3D12GraphicsCommandList* (D3D12CommandContext::*)() const noexcept;
using GetAllocatorSignature = ID3D12CommandAllocator* (D3D12CommandContext::*)() const noexcept;
using GetTypeSignature = D3D12_COMMAND_LIST_TYPE (D3D12CommandContext::*)() const noexcept;
using IsOpenSignature = bool (D3D12CommandContext::*)() const noexcept;
using ResourceBarrierOneSignature = void (D3D12CommandContext::*)(
    const D3D12_RESOURCE_BARRIER&);
using ResourceBarrierManySignature = void (D3D12CommandContext::*)(
    UINT,
    const D3D12_RESOURCE_BARRIER*);

static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::Initialize),
    InitializeSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::Reset),
    ResetSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::Close),
    CloseSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::GetCommandList),
    GetCommandListSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::GetAllocator),
    GetAllocatorSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::GetType),
    GetTypeSignature>);
static_assert(std::is_same_v<
    decltype(&D3D12CommandContext::IsOpen),
    IsOpenSignature>);

} // namespace

TEST(CompatibilityV1121, TypedCommandFoundationKeepsCommandContextApi) {
    InitializeSignature initialize = &D3D12CommandContext::Initialize;
    ResetSignature reset = &D3D12CommandContext::Reset;
    CloseSignature close = &D3D12CommandContext::Close;
    GetCommandListSignature getList = &D3D12CommandContext::GetCommandList;
    GetAllocatorSignature getAllocator = &D3D12CommandContext::GetAllocator;
    GetTypeSignature getType = &D3D12CommandContext::GetType;
    IsOpenSignature isOpen = &D3D12CommandContext::IsOpen;

    ResourceBarrierOneSignature barrierOne =
        static_cast<ResourceBarrierOneSignature>(
            &D3D12CommandContext::ResourceBarrier);
    ResourceBarrierManySignature barrierMany =
        static_cast<ResourceBarrierManySignature>(
            &D3D12CommandContext::ResourceBarrier);

    CHECK(initialize != nullptr);
    CHECK(reset != nullptr);
    CHECK(close != nullptr);
    CHECK(getList != nullptr);
    CHECK(getAllocator != nullptr);
    CHECK(getType != nullptr);
    CHECK(isOpen != nullptr);
    CHECK(barrierOne != nullptr);
    CHECK(barrierMany != nullptr);
}
