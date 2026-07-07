# D3D12ShaderReflection

`D3D12ShaderReflection` provides lightweight reflection helpers for compiled shader bytecode.

## Include

```cpp
#include <D3D12Helper/D3D12Gpu/D3D12ShaderReflection.hpp>
```

It is also included by:

```cpp
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
```

## Basic usage

```cpp
using namespace D3D12CoreLib;

ShaderBytecode bytecode = CompileShaderFromSource_D3DCompile(
    hlslSource,
    "main",
    "vs_5_0");

ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);
```

The same API also accepts DXIL/container bytecode produced by DXC when `dxcompiler.dll` is available at runtime.

```cpp
ShaderBytecode bytecode = CompileShaderFromSource_Dxc(
    hlslSource,
    "main",
    "cs_6_0");

ShaderReflectionInfo reflection = ReflectShaderBytecode(bytecode);
```

## Resource binding inspection

```cpp
const ShaderResourceBindingInfo* texture = FindResourceBinding(reflection, "gTexture");
if (texture) {
    UINT bindPoint = texture->bindPoint;
    UINT space = texture->space;
}
```

The binding info includes:

- resource name
- shader input type
- return type
- view dimension
- bind point
- bind count
- register space
- flags

## Constant buffer inspection

```cpp
const ShaderConstantBufferInfo* cb = FindConstantBuffer(reflection, "CameraConstants");
if (cb) {
    const ShaderConstantBufferVariableInfo* viewProj =
        FindConstantBufferVariable(*cb, "viewProj");
}
```

The constant buffer info includes:

- buffer name
- buffer type
- size in bytes
- variable list

Each variable includes:

- variable name
- start offset
- size in bytes
- flags

## Input layout generation

For vertex shaders, input signature parameters can be converted to a simple input-layout description.

```cpp
auto elements = MakeInputLayoutElementsFromReflection(reflection);
auto descs = MakeD3D12InputElementDescs(elements);
```

`MakeInputLayoutElementsFromReflection` skips system-value inputs such as `SV_VertexID` and maps common component masks to `DXGI_FORMAT_R32...` formats.

The returned `D3D12_INPUT_ELEMENT_DESC` objects reference semantic name strings owned by the `elements` vector. Keep `elements` alive while using the descriptors.

## Compute shader thread group size

```cpp
ShaderReflectionInfo reflection = ReflectShaderBytecode(computeBytecode);
UINT x = reflection.threadGroupSizeX;
UINT y = reflection.threadGroupSizeY;
UINT z = reflection.threadGroupSizeZ;
```

## Scope

Reflection first tries `D3DReflect` for DXBC bytecode, then falls back to `IDxcUtils::CreateReflection` for DXIL/container bytecode. DXIL reflection requires `dxcompiler.dll` to be available at runtime.
