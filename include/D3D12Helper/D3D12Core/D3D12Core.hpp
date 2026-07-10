#pragma once
//
// D3D12Core.hpp - Layer 1 ファサード
//
// Device / Queue / Fence / CommandContext を束ねる。
// Descriptor や Resource 生成は上位ライブラリ D3D12Framework 側にある。
// （Descriptor Allocator はサブシステムごとに生成する方針のため Core は所有しない）
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>
#include <D3D12Helper/D3D12Core/D3D12CoreConfig.hpp>
#include <D3D12Helper/D3D12Core/D3D12DeviceContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Queue.hpp>
#include <D3D12Helper/D3D12Core/D3D12CommandContext.hpp>
#include <D3D12Helper/D3D12Core/D3D12Subresource.hpp>
#include <D3D12Helper/D3D12Core/D3D12BarrierBatch.hpp>

#include <memory>
#include <optional>

namespace D3D12CoreLib {

class D3D12Core {
public:
    D3D12Core() = default;
    ~D3D12Core() = default;

    D3D12Core(const D3D12Core&)            = delete;
    D3D12Core& operator=(const D3D12Core&) = delete;

    void Initialize(const D3D12CoreConfig& config = {});
    void InitializeWithAdapterLuid(LUID luid, const D3D12CoreConfig& config = {});

    // サブシステム（Window / D3D12Camera 等）が shared_ptr で共有保持するための生成口。
    // 1つの Core を全サブシステムで共有する想定。
    static std::shared_ptr<D3D12Core> CreateShared(const D3D12CoreConfig& config = {});
    static std::shared_ptr<D3D12Core> CreateSharedWithAdapterLuid(
        LUID luid, const D3D12CoreConfig& config = {});

    // --- サブオブジェクト ---
    D3D12DeviceContext&       DeviceContext()       noexcept { return m_deviceContext; }
    const D3D12DeviceContext& DeviceContext() const noexcept { return m_deviceContext; }

    D3D12Queue& DirectQueue() noexcept { return m_directQueue; }
    D3D12Queue& CopyQueue()   noexcept { return m_copyQueue; }
    D3D12Queue* ComputeQueue() noexcept { return m_computeQueue ? &*m_computeQueue : nullptr; }

    // --- よく使うショートカット ---
    ID3D12Device*       GetDevice()            const noexcept { return m_deviceContext.GetDevice(); }
    ID3D12CommandQueue* GetDirectCommandQueue() noexcept { return m_directQueue.Get(); }
    LUID                GetAdapterLuid()       const noexcept { return m_deviceContext.GetAdapterLuid(); }
    bool                IsSameAdapter(LUID other) const noexcept;

    // --- CommandContext 生成（各 Context は自前 Allocator を持つ） ---
    D3D12CommandContext CreateDirectContext();
    D3D12CommandContext CreateCopyContext();
    D3D12CommandContext CreateComputeContext();

    // 全 Queue をフルフラッシュして待つ。
    void WaitIdle();

private:
    void InitializeInternal(const D3D12CoreConfig& config, const LUID* luid);

    D3D12CoreConfig    m_config;
    D3D12DeviceContext m_deviceContext;

    D3D12Queue m_directQueue;
    D3D12Queue m_copyQueue;
    std::optional<D3D12Queue> m_computeQueue;

    bool m_hasCopyQueue = false;
    bool m_initialized  = false;
};

} // namespace D3D12CoreLib
