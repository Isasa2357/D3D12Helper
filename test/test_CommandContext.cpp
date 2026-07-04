//
// test_CommandContext.cpp - Allocator+List の Reset/Close ライフサイクル
//
#include "TestCommon.hpp"

using namespace D3D12CoreLib;

TEST(CommandContext, ResetCloseLifecycle) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();

    // 生成直後は Close 状態。
    CHECK(!ctx.IsOpen());
    CHECK(ctx.GetCommandList() != nullptr);
    CHECK(ctx.GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT);

    ctx.Reset();
    CHECK(ctx.IsOpen());
    ctx.Close();
    CHECK(!ctx.IsOpen());
}

TEST(CommandContext, ResetWhileOpenThrows) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    CHECK_THROWS(ctx.Reset());   // Open 状態での Reset は不正
    ctx.Close();
}

TEST(CommandContext, ExecuteEmptyList) {
    REQUIRE_CORE(core);
    D3D12CommandContext ctx = core->CreateDirectContext();
    ctx.Reset();
    ctx.Close();

    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core->DirectQueue().ExecuteCommandLists(1, lists);
    core->DirectQueue().WaitForFenceValue(core->DirectQueue().Signal());

    // 完了後は同じ Allocator を再利用できる。
    ctx.Reset();
    ctx.Close();
}
