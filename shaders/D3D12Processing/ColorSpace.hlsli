#ifndef D3D12_PROCESSING_COLOR_SPACE_HLSLI
#define D3D12_PROCESSING_COLOR_SPACE_HLSLI

#include "YuvPrimitives.hlsli"

// Compatibility wrappers for existing application shaders.
//
// DecodeYuv / EncodeYuv historically operated on normalized NV12-style 8-bit
// samples and did not receive a DXGI format. Keep that source contract intact.
// New fused shaders should call the format-aware D3D12* functions from
// YuvPrimitives.hlsli directly so P010 storage is handled correctly.

float3 YuvToRgbMatrix(float y, float u, float v, uint matrix)
{
    return D3D12YuvToRgbSignal(y, u, v, matrix);
}

float3 RgbToYuvMatrix(float3 rgb, uint matrix)
{
    return D3D12RgbToYuvSignal(rgb, matrix);
}

float3 DecodeYuv(float ySample, float2 uvSample, uint range, uint matrix)
{
    return D3D12DecodeYuvSample(
        ySample,
        uvSample,
        DXGI_FORMAT_NV12_VALUE,
        range,
        matrix);
}

float3 EncodeYuv(float3 rgb, uint range, uint matrix)
{
    return D3D12EncodeRgbToYuvSample(
        rgb,
        DXGI_FORMAT_NV12_VALUE,
        range,
        matrix);
}

#endif
