#include <D3D12Helper/D3D12Interop/D3D12SharedHandle.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {

bool IsValidSharedHandle(HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

D3D12SharedHandle::~D3D12SharedHandle() {
    Reset();
}

D3D12SharedHandle::D3D12SharedHandle(D3D12SharedHandle&& other) noexcept
    : m_handle(other.Release()) {}

D3D12SharedHandle& D3D12SharedHandle::operator=(D3D12SharedHandle&& other) noexcept {
    if (this != &other) {
        Reset(other.Release());
    }
    return *this;
}

bool D3D12SharedHandle::IsValid() const noexcept {
    return IsValidSharedHandle(m_handle);
}

HANDLE D3D12SharedHandle::Release() noexcept {
    HANDLE h = m_handle;
    m_handle = nullptr;
    return h;
}

void D3D12SharedHandle::Reset(HANDLE handle) noexcept {
    if (IsValidSharedHandle(m_handle)) {
        CloseHandle(m_handle);
    }
    m_handle = handle;
}

D3D12SharedHandle TakeSharedHandle(HANDLE handle) noexcept {
    return D3D12SharedHandle(handle);
}

D3D12SharedHandle DuplicateSharedHandle(HANDLE handle) {
    if (!IsValidSharedHandle(handle)) {
        throw std::runtime_error("DuplicateSharedHandle: invalid handle");
    }
    HANDLE duplicated = nullptr;
    const BOOL ok = DuplicateHandle(GetCurrentProcess(), handle,
                                    GetCurrentProcess(), &duplicated,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
    if (!ok) {
        D3D12CORE_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }
    return D3D12SharedHandle(duplicated);
}

} // namespace D3D12CoreLib
