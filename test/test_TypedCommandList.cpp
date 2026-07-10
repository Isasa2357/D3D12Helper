//
// test_TypedCommandList.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12CommandAllocatorContext.hpp>

#include <array>
#include <type_traits>
#include <utility>

#if defined(__has_include)
#  if __has_include(<d3d12video.h>)
#    include <d3d12video.h>
#    define D3D12HELPER_TEST_HAS_D3D12VIDEO 1
#  endif
#endif

using namespace D3D12CoreLib;

namespace {

static_assert(!std::is_copy_constructible_v<D3D12CommandAllocatorContext>);
static_assert(!std::is_copy_assignable_v<D3D12CommandAllocatorContext>);
static_assert(std::is_move_constructible_v<D3D12CommandAllocatorContext>);
static_assert(std::is_move_assignable_v<D3D12CommandAllocatorContext>);

using GraphicsListFactoryResult = decltype(
    CreateTypedCommandList<ID3D12GraphicsCommandList>(
        std::declval<ID3D12Device*>(),
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        std::declval<ID3D12CommandAllocator*>()));
static_assert(std::is_same_v<
    GraphicsListFactoryResult,
    ComPtr<ID3D12GraphicsCommandList>>);

#ifdef D3D12HELPER_TEST_HAS_D3D12VIDEO
// The library header itself does not include d3d12video.h.  When an application
// supplies the SDK declaration, the same template is usable for a specialized
// command-list IID without any video-specific D3D12Helper type.
using VideoDecodeListFactoryResult = decltype(
    CreateTypedCommandList<ID3D12VideoDecodeCommandList>(
        std::declval<ID3D12Device*>(),
        D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
        std::declval<ID3D12CommandAllocator*>()));
static_assert(std::is_same_v<
    VideoDecodeListFactoryResult,
    ComPtr<ID3D12VideoDecodeCommandList>>);
#endif

void CloseList(ID3D12GraphicsCommandList* list) {
    if (!list) {
        TEST_FAIL("CloseList: list is null");
    }
    D3D12CORE_THROW_IF_FAILED(list->Close());
}

} // namespace

TEST(TypedCommandList, AllocatorContextLifecycle) {
    REQUIRE_CORE(core);

    D3D12CommandAllocatorContext allocator;
    CHECK(!allocator.IsInitialized());
    CHECK(allocator.GetAllocator() == nullptr);
    CHECK_THROWS(allocator.Reset());

    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    CHECK(allocator.IsInitialized());
    CHECK(allocator.GetAllocator() != nullptr);
    CHECK(allocator.GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT);

    // No command list has been submitted with this allocator, so reset is safe.
    CHECK_NOTHROW(allocator.Reset());

    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_COPY);
    CHECK(allocator.GetType() == D3D12_COMMAND_LIST_TYPE_COPY);
    CHECK(allocator.GetAllocator() != nullptr);
}

TEST(TypedCommandList, CreatesGraphicsComputeAndCopyLists) {
    REQUIRE_CORE(core);

    const std::array<D3D12_COMMAND_LIST_TYPE, 3> types = {
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY,
    };

    for (const auto type : types) {
        D3D12CommandAllocatorContext allocator;
        allocator.Initialize(core->GetDevice(), type);

        auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
            core->GetDevice(),
            type,
            allocator);

        CHECK(list.Get() != nullptr);
        CHECK(list->GetType() == type);
        CloseList(list.Get());

        // The list was never submitted, so allocator reuse is immediately safe.
        CHECK_NOTHROW(allocator.Reset());
    }
}

TEST(TypedCommandList, RawAllocatorOverloadCreatesList) {
    REQUIRE_CORE(core);

    D3D12CommandAllocatorContext allocator;
    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.GetAllocator());

    CHECK(list.Get() != nullptr);
    CHECK(list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT);
    CloseList(list.Get());
}

TEST(TypedCommandList, ContextOverloadRejectsTypeMismatch) {
    REQUIRE_CORE(core);

    D3D12CommandAllocatorContext allocator;
    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    CHECK_THROWS((
        CreateTypedCommandList<ID3D12GraphicsCommandList>(
            core->GetDevice(),
            D3D12_COMMAND_LIST_TYPE_COPY,
            allocator)));
}

TEST(TypedCommandList, RejectsNullArguments) {
    REQUIRE_CORE(core);

    D3D12CommandAllocatorContext allocator;
    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    CHECK_THROWS((
        CreateTypedCommandList<ID3D12GraphicsCommandList>(
            nullptr,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.GetAllocator())));

    CHECK_THROWS((
        CreateTypedCommandList<ID3D12GraphicsCommandList>(
            core->GetDevice(),
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            static_cast<ID3D12CommandAllocator*>(nullptr))));

    D3D12CommandAllocatorContext empty;
    CHECK_THROWS((
        CreateTypedCommandList<ID3D12GraphicsCommandList>(
            core->GetDevice(),
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            empty)));
}

TEST(TypedCommandList, AllocatorReuseAfterGpuCompletion) {
    REQUIRE_CORE(core);

    D3D12CommandAllocatorContext allocator;
    allocator.Initialize(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    auto list = CreateTypedCommandList<ID3D12GraphicsCommandList>(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator);
    CloseList(list.Get());

    ID3D12CommandList* lists[] = { list.Get() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(
        core->DirectQueue().Signal());

    // Fence completion makes allocator reset/reuse legal.
    CHECK_NOTHROW(allocator.Reset());

    auto second = CreateTypedCommandList<ID3D12GraphicsCommandList>(
        core->GetDevice(),
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator);
    CHECK(second.Get() != nullptr);
    CloseList(second.Get());
}
