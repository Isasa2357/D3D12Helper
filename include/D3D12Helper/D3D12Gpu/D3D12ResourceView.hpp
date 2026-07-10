#pragma once
//
// D3D12ResourceView.hpp
// Non-owning view of an ID3D12Resource.
//
// This type never calls AddRef / Release.  The referenced resource must remain
// alive for the complete duration of every operation that uses the view.
// Resource state is intentionally not stored here; callers that use the view
// with asynchronous or multi-queue work must provide explicit state data.
//
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

namespace D3D12CoreLib {

class D3D12ResourceView {
public:
    D3D12ResourceView() noexcept = default;

    explicit D3D12ResourceView(ID3D12Resource* resource) noexcept
        : m_resource(resource) {}

    explicit D3D12ResourceView(const D3D12Resource& resource) noexcept
        : m_resource(resource.Get()) {}

    ID3D12Resource* Get() const noexcept { return m_resource; }
    explicit operator bool() const noexcept { return m_resource != nullptr; }

    D3D12_RESOURCE_DESC GetDesc() const noexcept {
        return m_resource ? m_resource->GetDesc() : D3D12_RESOURCE_DESC{};
    }

    UINT64 GetWidth() const noexcept { return GetDesc().Width; }
    UINT GetHeight() const noexcept { return GetDesc().Height; }
    DXGI_FORMAT GetFormat() const noexcept { return GetDesc().Format; }

private:
    ID3D12Resource* m_resource = nullptr;
};

} // namespace D3D12CoreLib
