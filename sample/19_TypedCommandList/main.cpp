//
// 19_TypedCommandList / main.cpp
//
// Generic command allocator + typed command-list creation without any
// graphics/video-specific context class.
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <exception>
#include <iostream>

using namespace D3D12CoreLib;

int main() {
    try {
        D3D12CoreConfig config;
        config.enableDebugLayer = true;
        config.enableInfoQueue = true;
        config.allowWarpAdapter = true;
        config.createDirectQueue = true;
        config.createCopyQueue = false;
        config.createComputeQueue = false;

        auto core = D3D12Core::CreateShared(config);

        D3D12CommandAllocatorContext allocator;
        allocator.Initialize(
            core->GetDevice(),
            D3D12_COMMAND_LIST_TYPE_DIRECT);

        // CreateCommandList returns the list in the open/recording state.
        auto commandList =
            CreateTypedCommandList<ID3D12GraphicsCommandList>(
                core->GetDevice(),
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator);

        // Record command-list-specific operations here. This minimal sample
        // intentionally records no work.
        D3D12CORE_THROW_IF_FAILED(commandList->Close());

        ID3D12CommandList* lists[] = { commandList.Get() };
        core->DirectQueue().ExecuteCommandLists(1, lists);
        core->DirectQueue().WaitForFenceValue(
            core->DirectQueue().Signal());

        // Reset is legal only after every submitted use of the allocator has
        // completed. The fence wait above establishes that condition.
        allocator.Reset();

        std::cout
            << "Typed Direct command list completed; allocator is reusable.\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Typed command-list sample failed: "
                  << e.what() << "\n";
        return 1;
    }
}
