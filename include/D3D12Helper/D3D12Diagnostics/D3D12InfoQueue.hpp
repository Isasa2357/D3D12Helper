#pragma once
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

struct D3D12InfoQueueStats {
    bool available = false;
    UINT64 numStoredMessages = 0;
    UINT64 numMessagesAllowedByStorageFilter = 0;
    UINT64 numMessagesDeniedByStorageFilter = 0;
    UINT64 numMessagesDiscardedByMessageCountLimit = 0;
    UINT64 messageCountLimit = 0;
};

class D3D12InfoQueue {
public:
    bool Attach(ID3D12Device* device) noexcept;
    void Reset() noexcept { m_queue.Reset(); }
    bool IsAvailable() const noexcept { return m_queue != nullptr; }
    ID3D12InfoQueue* Get() const noexcept { return m_queue.Get(); }
    void Clear();
    void SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY severity, bool enable);
    D3D12InfoQueueStats GetStats() const noexcept;
private:
    ComPtr<ID3D12InfoQueue> m_queue;
};

D3D12InfoQueueStats GetD3D12InfoQueueStats(ID3D12Device* device) noexcept;

} // namespace D3D12CoreLib
