# D3D12Gpu

`D3D12Gpu`は、Resource、Descriptor、Upload、Readback、Transfer、Binding、Copy、Resolve、Mipmap、View、State、Pipeline、ShaderCompiler、Validationを提供するcanonical moduleです。

```cpp
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
```

すべての型・関数は名前空間`D3D12CoreLib`に属します。

## v1.13.0 Generic GPU Foundation

### 詳細Resource生成

既存の簡易`CreateBuffer()` / `CreateTexture2D()`に加え、committed Resourceの詳細設定を行う別名APIを提供します。

```cpp
D3D12BufferCreateDesc bufferDesc;
bufferDesc.sizeBytes = 4 * 1024 * 1024;
bufferDesc.heapType = D3D12_HEAP_TYPE_DEFAULT;
bufferDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
bufferDesc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

auto buffer = CreateBufferDetailed(core, bufferDesc);
```

```cpp
D3D12Texture2DCreateDesc textureDesc;
textureDesc.width = 1920;
textureDesc.height = 1080;
textureDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
textureDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
textureDesc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

auto texture = CreateTexture2DDetailed(core, textureDesc);
```

主な指定項目:

- alignment,
- heap type / CPU page property / memory pool,
- creation / visible node mask,
- heap flags,
- resource flags,
- initial state,
- Texture2D sample count / quality / layout,
- optional clear value.

`CreateBufferDetailed()`と`CreateTexture2DDetailed()`はcommitted Resource専用です。implicit heapでruntimeが決定する`DENY_*` / `ALLOW_ONLY_*`分類flagはD3D12呼び出し前に拒否します。

既存関数と同名のoverloadを追加せず、`&CreateBuffer`や`&CreateTexture2D`の一意性を維持します。

### Texture2D validation

```cpp
D3D12Texture2DRequirement requirement;
requirement.width = 1920;
requirement.height = 1080;
requirement.format = DXGI_FORMAT_NV12;
requirement.widthMultiple = 2;
requirement.heightMultiple = 2;

D3D12ValidationResult result = ValidateTexture2D(resource.Get(), requirement);
if (!result) {
    // result.errorsに複数の不一致が格納される。
}

ValidateTexture2DOrThrow(resource.Get(), requirement);
```

検証項目:

- null / dimension,
- width / height,
- format,
- array size / mip levels / sample count,
- required / forbidden Resource flags,
- width / heightの倍数制約,
- Device identity.

### 非所有Resource View

```cpp
D3D12ResourceView view(externalResource);
```

`D3D12ResourceView`はraw `ID3D12Resource*`だけを保持します。

- `AddRef` / `Release`を行わない。
- state cacheを持たない。
- pointer-sizeの軽量view。
- Resource lifetimeは呼び出し側が管理する。

ValidationとProcessing descriptor作成にはView専用経路があります。

### 範囲指定Readback

```cpp
auto mapped = readback.MapRead(offset, size);
const std::byte* data = mapped.Data();
```

`D3D12MappedReadRange`はmove-only RAII型です。

- 指定範囲をD3D12 read rangeへ反映。
- 範囲外をMap前に拒否。
- `size == 0`を空rangeとして許可。
- 破棄時にwritten range `{0, 0}`でUnmap。
- 既存の手動`Map()` / `Unmap()`と同時使用不可。

## 既存module構成

### Resource / Descriptor

- `D3D12Resource`
- `D3D12DescriptorHeap`
- `D3D12DescriptorAllocator`
- SRV / UAV / CBV / RTV / DSV helper

### Transfer

- `D3D12UploadBuffer`
- `D3D12ReadbackBuffer`
- `D3D12UploadRing`
- texture / buffer upload helper
- CPU image / texture transfer

### Pipeline / Binding

- `D3D12ComputePipeline`
- `D3D12GraphicsPipeline`
- `D3D12BindingSet`
- `D3D12DescriptorHeapSet`
- ShaderCompiler / ShaderReflection

### Copy / Resolve / Mipmap / View / State

- `D3D12Copy`
- `D3D12Resolve`
- `D3D12Mipmap`
- `D3D12View`
- `D3D12State`

`D3D12Framework`はv1.x compatibility wrapperとして引き続き利用できます。
