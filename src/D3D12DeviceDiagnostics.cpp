#include <D3D12Helper/D3D12Diagnostics/D3D12DeviceDiagnostics.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {

const char* DeviceRemovedReasonName(HRESULT reason) noexcept {
    switch (reason) {
    case S_OK: return "S_OK";
    case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
    default: return "UNKNOWN_HRESULT";
    }
}

bool IsDeviceRemovedReason(HRESULT reason) noexcept {
    return reason == DXGI_ERROR_DEVICE_HUNG ||
           reason == DXGI_ERROR_DEVICE_REMOVED ||
           reason == DXGI_ERROR_DEVICE_RESET ||
           reason == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

D3D12DeviceRemovedInfo CheckDeviceRemoved(ID3D12Device* device) {
    if (!device) {
        return D3D12DeviceRemovedInfo{ E_POINTER, "E_POINTER", true };
    }
    const HRESULT reason = device->GetDeviceRemovedReason();
    return D3D12DeviceRemovedInfo{ reason, DeviceRemovedReasonName(reason), IsDeviceRemovedReason(reason) };
}

void ThrowIfDeviceRemoved(ID3D12Device* device, const char* context) {
    const auto info = CheckDeviceRemoved(device);
    if (!info.removed) return;
    std::string message;
    if (context && *context) {
        message += context;
        message += ": ";
    }
    message += "D3D12 device removed: ";
    message += info.reasonName;
    throw std::runtime_error(message);
}

} // namespace D3D12CoreLib
