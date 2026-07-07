#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

class D3D12SharedHandle {
public:
    D3D12SharedHandle() = default;
    explicit D3D12SharedHandle(HANDLE handle) noexcept : m_handle(handle) {}
    ~D3D12SharedHandle();

    D3D12SharedHandle(const D3D12SharedHandle&) = delete;
    D3D12SharedHandle& operator=(const D3D12SharedHandle&) = delete;

    D3D12SharedHandle(D3D12SharedHandle&& other) noexcept;
    D3D12SharedHandle& operator=(D3D12SharedHandle&& other) noexcept;

    bool IsValid() const noexcept;
    explicit operator bool() const noexcept { return IsValid(); }
    HANDLE Get() const noexcept { return m_handle; }
    HANDLE Release() noexcept;
    void Reset(HANDLE handle = nullptr) noexcept;

private:
    HANDLE m_handle = nullptr;
};

bool IsValidSharedHandle(HANDLE handle) noexcept;
D3D12SharedHandle TakeSharedHandle(HANDLE handle) noexcept;
D3D12SharedHandle DuplicateSharedHandle(HANDLE handle);

} // namespace D3D12CoreLib
