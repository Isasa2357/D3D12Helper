#pragma once
//
// D3D12CommandAllocatorContext.hpp
// Command-list-type-aware allocator ownership and thin typed list creation.
//
// The helper intentionally knows nothing about graphics, video, decode, or
// encode-specific recording commands.  A caller may instantiate
// CreateTypedCommandList<TCommandList>() for any SDK command-list interface
// whose declaration is visible at the call site.
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace D3D12CoreLib {

class D3D12CommandAllocatorContext {
public:
    D3D12CommandAllocatorContext() = default;
    ~D3D12CommandAllocatorContext() = default;

    D3D12CommandAllocatorContext(const D3D12CommandAllocatorContext&) = delete;
    D3D12CommandAllocatorContext& operator=(const D3D12CommandAllocatorContext&) = delete;
    D3D12CommandAllocatorContext(D3D12CommandAllocatorContext&&) noexcept = default;
    D3D12CommandAllocatorContext& operator=(D3D12CommandAllocatorContext&&) noexcept = default;

    void Initialize(
        ID3D12Device* device,
        D3D12_COMMAND_LIST_TYPE type);

    // The caller must guarantee that every GPU command list previously recorded
    // with this allocator has completed before Reset() is called.
    void Reset();

    ID3D12CommandAllocator* GetAllocator() const noexcept {
        return m_allocator.Get();
    }

    D3D12_COMMAND_LIST_TYPE GetType() const noexcept {
        return m_type;
    }

    bool IsInitialized() const noexcept {
        return m_allocator != nullptr;
    }

private:
    ComPtr<ID3D12CommandAllocator> m_allocator;
    D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
};

// Creates one command list in the initial recording/open state defined by
// ID3D12Device::CreateCommandList.  Closing, resetting, and command-list-specific
// operations remain the caller's responsibility.
//
// This overload accepts a raw allocator.  D3D12 does not expose the allocator's
// creation type, so a type mismatch is reported by CreateCommandList itself.
template<class TCommandList>
ComPtr<TCommandList> CreateTypedCommandList(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    ID3D12CommandAllocator* allocator) {

    if (!device) {
        throw std::invalid_argument(
            "CreateTypedCommandList: device is null");
    }
    if (!allocator) {
        throw std::invalid_argument(
            "CreateTypedCommandList: allocator is null");
    }

    ComPtr<TCommandList> commandList;
    D3D12CORE_THROW_IF_FAILED_MSG(
        device->CreateCommandList(
            0,
            type,
            allocator,
            nullptr,
            IID_PPV_ARGS(commandList.GetAddressOf())),
        "CreateTypedCommandList: failed to create the requested command-list interface");
    return commandList;
}

// Context overload: validates the requested list type before entering D3D12.
template<class TCommandList>
ComPtr<TCommandList> CreateTypedCommandList(
    ID3D12Device* device,
    D3D12_COMMAND_LIST_TYPE type,
    const D3D12CommandAllocatorContext& allocatorContext) {

    if (!allocatorContext.IsInitialized()) {
        throw std::invalid_argument(
            "CreateTypedCommandList: allocator context is not initialized");
    }
    if (allocatorContext.GetType() != type) {
        throw std::invalid_argument(
            "CreateTypedCommandList: command-list type does not match allocator context type");
    }

    return CreateTypedCommandList<TCommandList>(
        device,
        type,
        allocatorContext.GetAllocator());
}

} // namespace D3D12CoreLib
