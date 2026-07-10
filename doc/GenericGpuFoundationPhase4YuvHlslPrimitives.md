# Generic GPU Foundation Phase 4: YUV HLSL Primitives

Phase 4 exposes the color-conversion and resize building blocks used by the Processing layer as an includeable HLSL function library. The intended use case is an application-owned shader that fuses conversion, resize, and an application-specific effect into one dispatch.

## Files

- `shaders/D3D12Processing/YuvPrimitives.hlsli`
  - NV12 and P010 storage/code conversion
  - BT.601, BT.709, and BT.2020 conversion
  - full-range and limited-range conversion
  - point and linear YUV420 resize sampling
  - point and linear logical-RGBA sampling
  - NV12/P010 luma and chroma store helpers
- `shaders/D3D12Processing/ColorSpace.hlsli`
  - retains the previous implementation and behavior of `DecodeYuv` / `EncodeYuv`
  - existing application shaders may continue including it unchanged
- `shaders/D3D12Processing/YuvPrimitiveProbe.hlsl`
  - test-only compute entry point for GPU golden values

The complete `shaders/` directory is already copied into build runtime directories and installed with D3D12Helper.

## P010 storage model

A P010 plane view is exposed as `R16_UNORM` or `R16G16_UNORM`, but a P010 video code occupies bits 15:6 of the 16-bit storage word. Treating the sample as an ordinary 16-bit normalized ten-bit value introduces range errors.

The primitive library explicitly converts between:

```text
10-bit code <-> (code << 6) / 65535
```

`D3D12YuvSampleToCode` and `D3D12YuvCodeToSample` perform this conversion. NV12 uses the corresponding 8-bit `code / 255` representation.

## Color equations

The library supports:

- BT.601: `Kr = 0.2990`, `Kb = 0.1140`
- BT.709: `Kr = 0.2126`, `Kb = 0.0722`
- BT.2020: `Kr = 0.2627`, `Kb = 0.0593`

Limited range is generalized from the standard 8-bit code ranges by multiplying them by four for P010:

```text
Y:  16..235  / 64..940
Cb: 16..240  / 64..960, neutral 128 / 512
Cr: 16..240  / 64..960, neutral 128 / 512
```

Full range uses `0..255` or `0..1023`, with chroma neutral at `128` or `512`.

## Main functions

Pure color functions:

```hlsl
float3 D3D12DecodeYuvCode(...);
float3 D3D12DecodeYuvSample(...);
float3 D3D12EncodeRgbToYuvCode(...);
float3 D3D12EncodeRgbToYuvSample(...);
```

YUV420 loading and resize:

```hlsl
float3 D3D12LoadYuv420Rgb(...);
float3 D3D12SampleYuv420RgbPoint(...);
float3 D3D12SampleYuv420RgbLinear(...);
```

RGB loading and resize:

```hlsl
float4 D3D12LoadLogicalRgba(...);
float4 D3D12SampleLogicalRgbaPoint(...);
float4 D3D12SampleLogicalRgbaLinear(...);
```

YUV420 output:

```hlsl
void D3D12StoreYuv420Luma(...);
void D3D12StoreYuv420Chroma(...);
```

## Fused shader example

```hlsl
#include "ProcessingCommon.hlsli"
#include "YuvPrimitives.hlsli"

Texture2D<float> YPlane : register(t0);
Texture2D<float2> UVPlane : register(t1);
RWTexture2D<float4> Dst : register(u0);

float3 rgb = D3D12SampleYuv420RgbLinear(
    YPlane,
    UVPlane,
    destinationPixel,
    uint2(SrcX, SrcY),
    uint2(SrcWidth, SrcHeight),
    float2(ScaleX, ScaleY),
    SrcFormat,
    SrcRange,
    SrcMatrix);

// Application-specific operations can be inserted here.
Dst[destinationPixel] = float4(rgb, 1.0f);
```

`sample/18_ProcessingCustomFusedShader` uses this path for YUV420 -> RGB -> resize -> outside-region darken in one dispatch. Its executable currently creates an NV12 input, while the included shader path also accepts P010 through `SrcFormat`.

## Existing Processing integration

The following built-in shaders now use the same primitives:

- `ConvertNv12ToRgb.hlsl`
- `ConvertRgbToNv12.hlsl`
- `FusedYuv420ToRgbResize.hlsl`
- `FusedRgbToRgbResize.hlsl`

The filenames remain unchanged for source and runtime compatibility. Despite their historical `Nv12` names, format-dependent paths select NV12 or P010 from `SrcFormat` / `DstFormat`.

`ColorSpace.hlsli` is intentionally not redirected to the format-aware functions. This preserves the previous numeric behavior for application shaders already calling the four-argument `DecodeYuv` and three-argument `EncodeYuv` functions. New shaders should include `YuvPrimitives.hlsli` and pass the actual format explicitly.

## Tests

The `YuvHlslPrimitives` suite includes:

- CPU golden codes for limited-range red in BT.601, BT.709, and BT.2020
- 8-bit NV12 and 10-bit P010 code paths
- P010 high-bit storage round trips
- CPU encode/decode round trips across matrix, range, and format combinations
- compilation of all refactored Processing shaders
- GPU output comparison against CPU reference values
- readback of actual NV12 plane bytes after RGBA -> NV12 conversion
- readback of actual P010 high-bit storage words after RGBA -> P010 conversion

No existing C++ public function or type is removed or overloaded by this phase.
