#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>

#include <iostream>

int main() {
    std::cout << "D3D12Helper package smoke test\n";
    D3D12CoreLib::D3D12CoreConfig config;
    config.allowWarpAdapter = true;
    return 0;
}
