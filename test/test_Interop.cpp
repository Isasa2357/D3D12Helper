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

    CHECK_THROWS(OpenSharedResource(core->GetDevice(), nullptr));
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

    CHECK_THROWS(OpenSharedFence(core->GetDevice(), nullptr));
}
