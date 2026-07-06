#pragma once
//
// D3D12SharedResource.hpp
// 共有 Handle の作成 / オープン（D3D11/D3D12共有・外部API連携の低レイヤ補助）。
// D3D12Core 自体は外部 API（CUDA/Varjo等）に依存しない。開く側の処理は別モジュールに置く。
//
#include <D3D12Helper/D3D12Core/D3D12Common.hpp>

namespace D3D12CoreLib {

class D3D12SharedResource {
public:
    // resource は D3D12_HEAP_FLAG_SHARED を付けて作成されている必要がある。
    // 戻り値の HANDLE は呼び出し側が CloseHandle で解放すること。
    static HANDLE CreateSharedHandle(
        ID3D12Device* device,
        ID3D12Resource* resource,
        LPCWSTR name = nullptr);

    static ComPtr<ID3D12Resource> OpenSharedHandle(
        ID3D12Device* device,
        HANDLE handle);

    // 共有リソースを Texture2D として開く convenience API。
    // D3D12 には Texture2D 専用 interface がないため戻り値は ID3D12Resource のまま、
    // desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D を検証する。
    static ComPtr<ID3D12Resource> OpenSharedTexture2D(
        ID3D12Device* device,
        HANDLE handle);
};

} // namespace D3D12CoreLib
