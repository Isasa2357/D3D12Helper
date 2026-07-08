#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>

using namespace D3D12CoreLib;

TEST(Diagnostics, DeviceRemovedHelpers) {
    CHECK(!IsDeviceRemovedReason(S_OK));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DEVICE_REMOVED));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DEVICE_HUNG));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DEVICE_RESET));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DRIVER_INTERNAL_ERROR));
    CHECK(!IsDeviceRemovedReason(DXGI_ERROR_INVALID_CALL));

    CHECK(std::string(DeviceRemovedReasonName(S_OK)) == "S_OK");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_DEVICE_HUNG)) == "DXGI_ERROR_DEVICE_HUNG");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_DEVICE_REMOVED)) == "DXGI_ERROR_DEVICE_REMOVED");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_DEVICE_RESET)) == "DXGI_ERROR_DEVICE_RESET");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_DRIVER_INTERNAL_ERROR)) == "DXGI_ERROR_DRIVER_INTERNAL_ERROR");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_INVALID_CALL)) == "DXGI_ERROR_INVALID_CALL");
    CHECK(std::string(DeviceRemovedReasonName(E_FAIL)) == "UNKNOWN_HRESULT");

    auto nullInfo = CheckDeviceRemoved(nullptr);
    CHECK(nullInfo.removed);
    CHECK(nullInfo.reason == E_POINTER);
    CHECK(std::string(nullInfo.reasonName) == "E_POINTER");
    CHECK_THROWS(ThrowIfDeviceRemoved(nullptr, "test"));
    CHECK_THROWS(ThrowIfDeviceRemoved(nullptr, nullptr));
    CHECK_THROWS(ThrowIfDeviceRemoved(nullptr, ""));
}

TEST(Diagnostics, DeviceRemovedLiveDevice) {
    REQUIRE_CORE(core);
    auto info = CheckDeviceRemoved(core->GetDevice());
    CHECK(info.reason == S_OK);
    CHECK(!info.removed);
    CHECK(std::string(info.reasonName) == "S_OK");
    CHECK_NOTHROW(ThrowIfDeviceRemoved(core->GetDevice(), "live device"));
}

TEST(Diagnostics, DebugHelpersNoThrow) {
    REQUIRE_CORE(core);
    CHECK_NOTHROW(D3D12Debug::EnableDebugLayer(false));
    CHECK_NOTHROW(D3D12Debug::EnableDebugLayer(true));
    CHECK_NOTHROW(D3D12Debug::EnableDred());
    CHECK_NOTHROW(D3D12Debug::SetupInfoQueue(nullptr, true, true, true));
    CHECK_NOTHROW(D3D12Debug::SetupInfoQueue(core->GetDevice(), false, false, false));
    CHECK_NOTHROW(D3D12Debug::SetupInfoQueue(core->GetDevice(), true, true, true));
    CHECK_NOTHROW(D3D12Debug::PrintDredInfo(nullptr));
    CHECK_NOTHROW(D3D12Debug::PrintDredInfo(core->GetDevice()));
    D3D12Debug::SetDebugName<ID3D12Device>(nullptr, L"null device");
    D3D12Debug::SetDebugName(core->GetDevice(), nullptr);
    D3D12Debug::SetDebugName(core->GetDevice(), L"D3D12HelperDiagnosticsTestDevice");
}

TEST(Diagnostics, InfoQueueOptional) {
    REQUIRE_CORE(core);

    D3D12InfoQueue queue;
    CHECK(!queue.IsAvailable());
    CHECK(queue.Get() == nullptr);
    CHECK(!queue.Attach(nullptr));
    CHECK(!queue.IsAvailable());
    CHECK_THROWS(queue.Clear());
    CHECK_THROWS(queue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false));

    const bool available = queue.Attach(core->GetDevice());
    CHECK_EQ(queue.IsAvailable(), available);
    CHECK_EQ(queue.GetStats().available, available);

    auto nullStats = GetD3D12InfoQueueStats(nullptr);
    CHECK(!nullStats.available);
    CHECK_EQ(nullStats.numStoredMessages, 0ull);
    CHECK_EQ(nullStats.messageCountLimit, 0ull);

    auto deviceStats = GetD3D12InfoQueueStats(core->GetDevice());
    CHECK_EQ(deviceStats.available, available);

    if (available) {
        queue.Clear();
        queue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
        queue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
        queue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
        auto stats = queue.GetStats();
        CHECK(stats.available);
        CHECK(stats.messageCountLimit >= 0);
    }

    queue.Reset();
    CHECK(!queue.IsAvailable());
    CHECK(!queue.GetStats().available);
}

TEST(Diagnostics, GpuTimerBasic) {
    REQUIRE_CORE(core);

    D3D12GpuTimer timer;
    timer.Initialize(core->GetDevice());

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    timer.Begin(ctx.GetCommandList());
    timer.End(ctx.GetCommandList());
    timer.Resolve(ctx.GetCommandList());
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();

    const double ms = timer.GetElapsedMilliseconds(core->DirectQueue().Get());
    CHECK(ms >= 0.0);
}

TEST(Diagnostics, GpuProfilerBasic) {
    REQUIRE_CORE(core);

    D3D12GpuProfiler profiler;
    profiler.Initialize(core->GetDevice(), 2);

    auto ctx = core->CreateDirectContext();
    ctx.Reset();
    const UINT section = profiler.BeginSection(ctx.GetCommandList(), "empty");
    profiler.EndSection(ctx.GetCommandList(), section);
    profiler.Resolve(ctx.GetCommandList());
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitIdle();

    auto results = profiler.GetResults(core->DirectQueue().Get());
    CHECK_EQ(results.size(), static_cast<size_t>(1));
    CHECK(results[0].name == "empty");
    CHECK(results[0].milliseconds >= 0.0);
}
