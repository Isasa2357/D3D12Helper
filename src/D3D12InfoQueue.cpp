#include <D3D12Helper/D3D12Diagnostics/D3D12InfoQueue.hpp>

#include <stdexcept>
#include <string>

namespace D3D12CoreLib {

bool D3D12InfoQueue::Attach(ID3D12Device* device) noexcept {
    Reset();
    if (!device) return false;
    return SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&m_queue)));
}

void D3D12InfoQueue::Clear() {
    if (!m_queue) throw std::runtime_error("D3D12InfoQueue::Clear: InfoQueue is not available");
    m_queue->ClearStoredMessages();
}

void D3D12InfoQueue::SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY severity, bool enable) {
    if (!m_queue) throw std::runtime_error("D3D12InfoQueue::SetBreakOnSeverity: InfoQueue is not available");
    m_queue->SetBreakOnSeverity(severity, enable ? TRUE : FALSE);
}

D3D12InfoQueueStats D3D12InfoQueue::GetStats() const noexcept {
    D3D12InfoQueueStats stats;
    if (!m_queue) return stats;
    stats.available = true;
    stats.numStoredMessages = m_queue->GetNumStoredMessages();
    stats.numMessagesAllowedByStorageFilter = m_queue->GetNumMessagesAllowedByStorageFilter();
    stats.numMessagesDeniedByStorageFilter = m_queue->GetNumMessagesDeniedByStorageFilter();
    stats.numMessagesDiscardedByMessageCountLimit = m_queue->GetNumMessagesDiscardedByMessageCountLimit();
    stats.messageCountLimit = m_queue->GetMessageCountLimit();
    return stats;
}

D3D12InfoQueueStats GetD3D12InfoQueueStats(ID3D12Device* device) noexcept {
    D3D12InfoQueue queue;
    if (!queue.Attach(device)) return {};
    return queue.GetStats();
}

} // namespace D3D12CoreLib
