#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>

using namespace D3D12CoreLib;

TEST(Interop, SharedHandleRaii) {
    HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    CHECK(eventHandle != nullptr);

    D3D12SharedHandle owned(eventHandle);
    CHECK(owned.IsValid());
    CHECK(IsValidSharedHandle(owned.Get()));

    auto duplicated = DuplicateSharedHandle(owned.Get());
    CHECK(duplicated.IsValid());

    HANDLE raw = owned.Release();
    CHECK(raw == eventHandle);
    CHECK(!owned.IsValid());
    CloseHandle(raw);
}

TEST(Interop, SharedHandleValidationAndMove) {
    CHECK(!IsValidSharedHandle(nullptr));
    CHECK(!IsValidSharedHandle(INVALID_HANDLE_VALUE));
    CHECK_THROWS(DuplicateSharedHandle(nullptr));
    CHECK_THROWS(DuplicateSharedHandle(INVALID_HANDLE_VALUE));

    HANDLE a = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    HANDLE b = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    CHECK(a != nullptr);
    CHECK(b != nullptr);

    auto first = TakeSharedHandle(a);
    CHECK(first.IsValid());
    D3D12SharedHandle moved(std::move(first));
    CHECK(!first.IsValid());
    CHECK(moved.IsValid());

    D3D12SharedHandle assigned;
    assigned = std::move(moved);
    CHECK(!moved.IsValid());
    CHECK(assigned.IsValid());

    assigned.Reset(b);
    CHECK(assigned.Get() == b);
    assigned.Reset();
    CHECK(!assigned.IsValid());
}

TEST(Interop, SharedTextureOpenSameDevice) {
    REQUIRE_CORE(core);

    auto texture = CreateSharedTexture2DResource(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto handle = CreateSharedHandleForResource(core->GetDevice(), texture);
    CHECK(handle.IsValid());

    auto opened = OpenSharedResource(core->GetDevice(), handle.Get(), D3D12_RESOURCE_STATE_COMMON);
    CHECK(opened.Get() != nullptr);
    CHECK_EQ(opened.GetDesc().Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
    CHECK_EQ(static_cast<UINT>(opened.GetDesc().Width), 4u);
    CHECK_EQ(opened.GetDesc().Height, 4u);
    CHECK_EQ(opened.GetDesc().Format, DXGI_FORMAT_R8G8B8A8_UNORM);

    auto openedDirect = D3D12SharedResource::OpenSharedTexture2D(core->GetDevice(), handle.Get());
    CHECK(openedDirect != nullptr);
    CHECK_EQ(openedDirect->GetDesc().Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);

    CHECK_THROWS(OpenSharedResource(core->GetDevice(), nullptr));
}

TEST(Interop, SharedResourceValidation) {
    REQUIRE_CORE(core);

    auto sharedTexture = CreateSharedTexture2DResource(*core, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto sharedHandle = CreateSharedHandleForResource(core->GetDevice(), sharedTexture);
    D3D12Resource nullResource;

    CHECK_THROWS(CreateSharedHandleForResource(nullptr, sharedTexture));
    CHECK_THROWS(CreateSharedHandleForResource(core->GetDevice(), nullResource));
    CHECK_THROWS(OpenSharedResource(nullptr, sharedHandle.Get()));
    CHECK_THROWS(OpenSharedResource(core->GetDevice(), nullptr));
    CHECK_THROWS(OpenSharedResource(core->GetDevice(), INVALID_HANDLE_VALUE));
    CHECK_THROWS(D3D12SharedResource::OpenSharedHandle(core->GetDevice(), nullptr));

    auto nonShared = CreateTexture2D(*core, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COMMON);
    CHECK_THROWS(D3D12SharedResource::CreateSharedHandle(core->GetDevice(), nonShared.Get()));

    auto sharedBuffer = CreateBuffer(
        *core,
        256,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_HEAP_FLAG_SHARED);
    auto bufferHandle = CreateSharedHandleForResource(core->GetDevice(), sharedBuffer);
    CHECK(bufferHandle.IsValid());
    auto openedBuffer = OpenSharedResource(core->GetDevice(), bufferHandle.Get());
    CHECK(openedBuffer.Get() != nullptr);
    CHECK_EQ(openedBuffer.GetDesc().Dimension, D3D12_RESOURCE_DIMENSION_BUFFER);
    CHECK_THROWS(D3D12SharedResource::OpenSharedTexture2D(core->GetDevice(), bufferHandle.Get()));
}

TEST(Interop, SharedFenceOpenSameDevice) {
    REQUIRE_CORE(core);

    D3D12Fence fence;
    fence.InitializeShared(core->GetDevice());
    auto handle = CreateSharedHandleForFence(core->GetDevice(), fence);
    CHECK(handle.IsValid());

    auto opened = OpenSharedFence(core->GetDevice(), handle.Get());
    CHECK(opened.Get() != nullptr);

    fence.Signal(core->DirectQueue().Get(), 3);
    opened.Wait(3);
    CHECK(opened.GetCompletedValue() >= 3);

    CHECK_THROWS(CreateSharedHandleForFence(nullptr, fence));
    CHECK_THROWS(OpenSharedFence(nullptr, handle.Get()));
    CHECK_THROWS(OpenSharedFence(core->GetDevice(), nullptr));
    CHECK_THROWS(OpenSharedFence(core->GetDevice(), INVALID_HANDLE_VALUE));
}
