#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

struct D3D12DeviceRemovedInfo {
    HRESULT reason = S_OK;
    const char* reasonName = "S_OK";
    bool removed = false;
};

const char* DeviceRemovedReasonName(HRESULT reason) noexcept;
bool IsDeviceRemovedReason(HRESULT reason) noexcept;
D3D12DeviceRemovedInfo CheckDeviceRemoved(ID3D12Device* device);
void ThrowIfDeviceRemoved(ID3D12Device* device, const char* context = nullptr);

} // namespace D3D12CoreLib
