//
// D3D11InteropE2E.cpp - optional D3D12/D3D11 end-to-end interop tests.
//
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Interop/D3D12Interop.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>
#include <D3D11Helper/D3D11Gpu/D3D11Gpu.hpp>
#include <D3D11Helper/D3D11Interop/D3D11Interop.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void Run(const char* name, const std::function<void()>& fn) {
    try {
        fn();
        ++g_passed;
        std::cout << "[PASS] " << name << "\n";
    } catch (const std::exception& e) {
        ++g_failed;
        std::cout << "[FAIL] " << name << " -- " << e.what() << "\n";
    } catch (...) {
        ++g_failed;
        std::cout << "[FAIL] " << name << " -- unknown exception\n";
    }
}

std::shared_ptr<D3D11CoreLib::D3D11Core> MakeD3D11CoreForD3D12Adapter(D3D12CoreLib::D3D12Core& d3d12) {
    D3D11CoreLib::D3D11CoreConfig cfg;
    cfg.enableDebugLayer = true;
    cfg.enableInfoQueue = true;
    cfg.allowWarpAdapter = true;
    return D3D11CoreLib::D3D11Core::CreateSharedWithAdapterLuid(d3d12.GetAdapterLuid(), cfg);
}

D3D12CoreLib::D3D12CpuImage MakeD3D12Image(UINT width, UINT height) {
    auto image = D3D12CoreLib::CreateCpuImage(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    auto& p = image.planes[0];
    for (UINT y = 0; y < height; ++y) {
        auto* row = image.pixels.data() + p.offsetBytes + static_cast<size_t>(y) * p.rowPitch;
        for (UINT x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(x) * 4u;
            row[i + 0] = static_cast<uint8_t>(20 + x + y * 5u);
            row[i + 1] = static_cast<uint8_t>(50 + x * 2u + y);
            row[i + 2] = static_cast<uint8_t>(90 + x + y * 3u);
            row[i + 3] = 255;
        }
    }
    return image;
}

void CheckImagesEqual(const D3D12CoreLib::D3D12CpuImage& expected, const D3D11CoreLib::D3D11CpuImage& got) {
    if (expected.width != got.width || expected.height != got.height || expected.format != got.format) {
        throw std::runtime_error("image metadata mismatch");
    }
    if (expected.planes.empty() || got.planes.empty()) {
        throw std::runtime_error("missing image plane");
    }
    const auto& ep = expected.planes[0];
    const auto& gp = got.planes[0];
    const UINT rowBytes = expected.width * 4u;
    for (UINT y = 0; y < expected.height; ++y) {
        const auto* e = expected.pixels.data() + ep.offsetBytes + static_cast<size_t>(y) * ep.rowPitch;
        const auto* g = got.pixels.data() + gp.offsetBytes + static_cast<size_t>(y) * gp.rowPitch;
        for (UINT i = 0; i < rowBytes; ++i) {
            if (e[i] != g[i]) {
                throw std::runtime_error("pixel mismatch");
            }
        }
    }
}

void ExpectThrows(const char* label, const std::function<void()>& fn) {
    bool threw = false;
    try {
        fn();
    } catch (...) {
        threw = true;
    }
    if (!threw) {
        throw std::runtime_error(std::string(label) + " did not throw");
    }
}

} // namespace

int main() {
    D3D12CoreLib::D3D12CoreConfig cfg12;
    cfg12.enableDebugLayer = true;
    cfg12.enableInfoQueue = true;
    cfg12.allowWarpAdapter = true;
    cfg12.createCopyQueue = true;

    auto d3d12 = D3D12CoreLib::D3D12Core::CreateShared(cfg12);
    std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11;

    try {
        d3d11 = MakeD3D11CoreForD3D12Adapter(*d3d12);
    } catch (const std::exception& e) {
        std::cout << "[INFO] Skipping D3D12/D3D11 interop tests: " << e.what() << "\n";
        return 0;
    }

    Run("D3D12 shared texture opened by D3D11", [&] {
        constexpr UINT width = 4;
        constexpr UINT height = 4;
        auto expected = MakeD3D12Image(width, height);

        auto shared = D3D12CoreLib::CreateSharedTexture2DResource(
            *d3d12,
            width,
            height,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_NONE);

        D3D12CoreLib::UpdateTexture2DFromCpuImage(
            *d3d12,
            shared,
            expected,
            D3D12_RESOURCE_STATE_COMMON);

        auto handle = D3D12CoreLib::CreateSharedHandleForResource(d3d12->GetDevice(), shared);
        auto opened11 = D3D11CoreLib::OpenSharedTexture2D(d3d11->GetDevice(), handle.Get());
        if (!opened11) {
            throw std::runtime_error("D3D11 did not open D3D12 shared texture");
        }
        if (!d3d11->IsSameAdapter(d3d12->GetAdapterLuid())) {
            throw std::runtime_error("D3D11 core is not on the D3D12 adapter");
        }

        D3D11CoreLib::D3D11Resource openedResource(std::move(opened11));
        auto got = D3D11CoreLib::ReadbackTexture2DToCpuImage(*d3d11, openedResource);
        CheckImagesEqual(expected, got);
    });

    Run("D3D11 shared fence opened by D3D12", [&] {
        const auto support = D3D11CoreLib::CheckD3D11FenceSupport(
            d3d11->GetDevice(), d3d11->GetImmediateContext());
        if (!support.supported) {
            std::cout << "[INFO] Skipping shared fence E2E: " << support.reason << "\n";
            return;
        }

        D3D11CoreLib::D3D11Fence fence11;
        fence11.Initialize(d3d11->GetDevice());
        auto handle = fence11.CreateSharedHandleOwned();
        if (!handle) {
            throw std::runtime_error("invalid D3D11 shared fence handle");
        }

        auto fence12 = D3D12CoreLib::OpenSharedFence(d3d12->GetDevice(), handle.Get());
        fence11.Signal(d3d11->GetImmediateContext(), 7);
        d3d11->Flush();
        fence12.Wait(7);
        if (fence12.GetCompletedValue() < 7) {
            throw std::runtime_error("D3D12 fence did not observe D3D11 signal");
        }
    });

    Run("Interop invalid arguments", [&] {
        ExpectThrows("OpenSharedFence null device", [&] {
            (void)D3D12CoreLib::OpenSharedFence(nullptr, nullptr);
        });
        ExpectThrows("D3D11 OpenSharedTexture invalid handle", [&] {
            D3D11CoreLib::D3D11SharedHandle invalid;
            (void)D3D11CoreLib::OpenSharedTexture2D(d3d11->GetDevice(), invalid);
        });
    });

    std::cout << "\n--- InteropD3D11 ---\n"
              << "  Passed: " << g_passed << "\n"
              << "  Failed: " << g_failed << "\n";
    return g_failed ? 1 : 0;
}
