#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Diagnostics/D3D12Diagnostics.hpp>

using namespace D3D12CoreLib;

TEST(Diagnostics, DeviceRemovedHelpers) {
    CHECK(!IsDeviceRemovedReason(S_OK));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DEVICE_REMOVED));
    CHECK(IsDeviceRemovedReason(DXGI_ERROR_DEVICE_HUNG));
    CHECK(std::string(DeviceRemovedReasonName(S_OK)) == "S_OK");
    CHECK(std::string(DeviceRemovedReasonName(DXGI_ERROR_DEVICE_RESET)) == "DXGI_ERROR_DEVICE_RESET");

    auto nullInfo = CheckDeviceRemoved(nullptr);
    CHECK(nullInfo.removed);
    CHECK_THROWS(ThrowIfDeviceRemoved(nullptr, "test"));
}

TEST(Diagnostics, InfoQueueOptional) {
    REQUIRE_CORE(core);

    D3D12InfoQueue queue;
    const bool available = queue.Attach(core->GetDevice());
    auto stats = queue.GetStats();
    CHECK_EQ(stats.available, available);

    auto nullStats = GetD3D12InfoQueueStats(nullptr);
    CHECK(!nullStats.available);

    if (available) {
        queue.Clear();
        queue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
    }
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
