//
// D3D12ReadbackBuffer.cpp
//
#include <D3D12Helper/D3D12Framework/D3D12ReadbackBuffer.hpp>
#include <D3D12Helper/D3D12Core/ThrowIfFailed.hpp>

#include <stdexcept>
#include <utility>

namespace D3D12CoreLib {

namespace {

constexpr int kMapModeNone   = 0;
constexpr int kMapModeLegacy = 1;
constexpr int kMapModeScoped = 2;

} // namespace

D3D12MappedReadRange::D3D12MappedReadRange(
    ComPtr<ID3D12Resource> resource,
    std::shared_ptr<std::atomic<int>> mapMode,
    const std::byte* data,
    UINT64 offset,
    UINT64 size) noexcept
    : m_resource(std::move(resource))
    , m_mapMode(std::move(mapMode))
    , m_data(data)
    , m_offset(offset)
    , m_size(size) {}

D3D12MappedReadRange::~D3D12MappedReadRange() {
    Reset();
}

D3D12MappedReadRange::D3D12MappedReadRange(D3D12MappedReadRange&& other) noexcept
    : m_resource(std::move(other.m_resource))
    , m_mapMode(std::move(other.m_mapMode))
    , m_data(other.m_data)
    , m_offset(other.m_offset)
    , m_size(other.m_size) {
    other.m_data = nullptr;
    other.m_offset = 0;
    other.m_size = 0;
}

D3D12MappedReadRange& D3D12MappedReadRange::operator=(D3D12MappedReadRange&& other) noexcept {
    if (this != &other) {
        Reset();
        m_resource = std::move(other.m_resource);
        m_mapMode = std::move(other.m_mapMode);
        m_data = other.m_data;
        m_offset = other.m_offset;
        m_size = other.m_size;

        other.m_data = nullptr;
        other.m_offset = 0;
        other.m_size = 0;
    }
    return *this;
}

void D3D12MappedReadRange::Reset() noexcept {
    if (m_resource) {
        D3D12_RANGE empty = { 0, 0 };
        m_resource->Unmap(0, &empty);
    }

    if (m_mapMode) {
        m_mapMode->store(kMapModeNone, std::memory_order_release);
    }

    m_resource.Reset();
    m_mapMode.reset();
    m_data = nullptr;
    m_offset = 0;
    m_size = 0;
}

D3D12ReadbackBuffer::~D3D12ReadbackBuffer() {
    Destroy();
}

void D3D12ReadbackBuffer::Destroy() noexcept {
    if (m_resource && m_mappedPtr) {
        // v1.12.1 以前の destructor / move-assignment 経路と同じ written range を維持する。
        m_resource->Unmap(0, nullptr);
    }

    if (m_mapMode && m_mapMode->load(std::memory_order_acquire) == kMapModeLegacy) {
        m_mapMode->store(kMapModeNone, std::memory_order_release);
    }

    // scoped MapRead の最中でも、その range が Resource と map mode を保持している。
    // Buffer 側の所有を解放しても range の破棄時に安全に Unmap される。
    m_mappedPtr = nullptr;
    m_resource.Reset();
    m_sizeBytes = 0;
    m_mapMode.reset();
}

D3D12ReadbackBuffer::D3D12ReadbackBuffer(D3D12ReadbackBuffer&& other) noexcept
    : m_resource(std::move(other.m_resource))
    , m_mappedPtr(other.m_mappedPtr)
    , m_sizeBytes(other.m_sizeBytes)
    , m_mapMode(std::move(other.m_mapMode)) {
    other.m_mappedPtr = nullptr;
    other.m_sizeBytes = 0;
}

D3D12ReadbackBuffer& D3D12ReadbackBuffer::operator=(D3D12ReadbackBuffer&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_resource = std::move(other.m_resource);
        m_mappedPtr = other.m_mappedPtr;
        m_sizeBytes = other.m_sizeBytes;
        m_mapMode = std::move(other.m_mapMode);

        other.m_mappedPtr = nullptr;
        other.m_sizeBytes = 0;
    }
    return *this;
}

void D3D12ReadbackBuffer::EnsureMapMode() {
    if (!m_mapMode) {
        m_mapMode = std::make_shared<std::atomic<int>>(kMapModeNone);
    }
}

void D3D12ReadbackBuffer::Initialize(ID3D12Device* device, UINT64 sizeBytes) {
    if (!device) throw std::runtime_error("D3D12ReadbackBuffer: null device");
    if (sizeBytes == 0) throw std::runtime_error("D3D12ReadbackBuffer: size must be > 0");

    Destroy();
    EnsureMapMode();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type                 = D3D12_HEAP_TYPE_READBACK;
    hp.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask     = 1;
    hp.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width              = sizeBytes;
    desc.Height             = 1;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count   = 1;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    D3D12CORE_THROW_IF_FAILED(
        device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_resource)));

    m_sizeBytes = sizeBytes;
}

const void* D3D12ReadbackBuffer::Map() {
    if (!m_resource) throw std::runtime_error("D3D12ReadbackBuffer: not initialized");
    EnsureMapMode();

    const int currentMode = m_mapMode->load(std::memory_order_acquire);
    if (currentMode == kMapModeLegacy) {
        return m_mappedPtr;
    }
    if (currentMode == kMapModeScoped) {
        throw std::runtime_error("D3D12ReadbackBuffer::Map: MapRead range is already active");
    }

    int expected = kMapModeNone;
    if (!m_mapMode->compare_exchange_strong(
            expected, kMapModeLegacy,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        throw std::runtime_error("D3D12ReadbackBuffer::Map: buffer is already mapped");
    }

    try {
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_sizeBytes) };
        D3D12CORE_THROW_IF_FAILED(m_resource->Map(0, &range, &m_mappedPtr));
    } catch (...) {
        m_mappedPtr = nullptr;
        m_mapMode->store(kMapModeNone, std::memory_order_release);
        throw;
    }

    return m_mappedPtr;
}

void D3D12ReadbackBuffer::Unmap() {
    if (!m_mapMode) {
        return;
    }

    const int currentMode = m_mapMode->load(std::memory_order_acquire);
    if (currentMode == kMapModeScoped) {
        throw std::runtime_error("D3D12ReadbackBuffer::Unmap: MapRead range owns the active mapping");
    }

    if (m_resource && m_mappedPtr) {
        D3D12_RANGE empty = { 0, 0 };
        m_resource->Unmap(0, &empty);
        m_mappedPtr = nullptr;
    }

    if (currentMode == kMapModeLegacy) {
        m_mapMode->store(kMapModeNone, std::memory_order_release);
    }
}

D3D12MappedReadRange D3D12ReadbackBuffer::MapRead(UINT64 offset, UINT64 size) {
    if (!m_resource) throw std::runtime_error("D3D12ReadbackBuffer::MapRead: not initialized");

    if (offset > m_sizeBytes || size > (m_sizeBytes - offset)) {
        throw std::out_of_range("D3D12ReadbackBuffer::MapRead: requested range exceeds buffer size");
    }

    if (size == 0) {
        return D3D12MappedReadRange(offset);
    }

    EnsureMapMode();
    int expected = kMapModeNone;
    if (!m_mapMode->compare_exchange_strong(
            expected, kMapModeScoped,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        throw std::runtime_error("D3D12ReadbackBuffer::MapRead: buffer is already mapped");
    }

    void* mappedBase = nullptr;
    try {
        D3D12_RANGE range = {
            static_cast<SIZE_T>(offset),
            static_cast<SIZE_T>(offset + size)
        };
        D3D12CORE_THROW_IF_FAILED(m_resource->Map(0, &range, &mappedBase));
    } catch (...) {
        m_mapMode->store(kMapModeNone, std::memory_order_release);
        throw;
    }

    const auto* data = static_cast<const std::byte*>(mappedBase) + offset;
    return D3D12MappedReadRange(m_resource, m_mapMode, data, offset, size);
}

} // namespace D3D12CoreLib
