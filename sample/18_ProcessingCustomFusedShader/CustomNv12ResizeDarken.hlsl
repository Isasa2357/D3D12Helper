#include "ProcessingCommon.hlsli"
#include "YuvPrimitives.hlsli"

// Application-owned fused shader.
// This intentionally uses the D3D12Processing HLSL primitive library instead
// of a shortcut Processor class. It fuses:
//   NV12/P010 -> RGB -> resize -> circular outside-region darken.

Texture2D<float> YPlane : register(t0);
Texture2D<float2> UVPlane : register(t1);
RWTexture2D<float4> Dst : register(u0);

float OutsideCircleAmount(uint2 dstLocal)
{
    const float2 p = float2(dstLocal) + 0.5f;
    const float2 center = float2((float)DstWidth, (float)DstHeight) * 0.5f;
    const float radius = min((float)DstWidth, (float)DstHeight) * 0.35f;
    const float feather = max(2.0f, min((float)DstWidth, (float)DstHeight) * 0.04f);
    return smoothstep(radius, radius + feather, distance(p, center));
}

float3 ApplyOutsideDarken(uint2 dstLocal, float3 rgb)
{
    const float outside = OutsideCircleAmount(dstLocal);
    return lerp(rgb, rgb * 0.50f, outside);
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint2 p = tid.xy;
    if (!InRect(p, DstWidth, DstHeight)) {
        return;
    }

    const uint2 sourceOrigin = uint2((uint)SrcX, (uint)SrcY);
    const uint2 sourceSize = uint2(SrcWidth, SrcHeight);
    const float2 scale = float2(ScaleX, ScaleY);

    float3 rgb = (Filter == PROCESSING_FILTER_POINT)
        ? D3D12SampleYuv420RgbPoint(
            YPlane,
            UVPlane,
            p,
            sourceOrigin,
            sourceSize,
            scale,
            SrcFormat,
            SrcRange,
            SrcMatrix)
        : D3D12SampleYuv420RgbLinear(
            YPlane,
            UVPlane,
            p,
            sourceOrigin,
            sourceSize,
            scale,
            SrcFormat,
            SrcRange,
            SrcMatrix);

    rgb = ApplyOutsideDarken(p, rgb);
    Dst[OffsetPosition(DstX, DstY, p)] =
        FromLogicalRgba(saturate(float4(rgb, 1.0f)), DstFormat);
}
