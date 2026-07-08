#include "ProcessingCommon.hlsli"
#include "ColorSpace.hlsli"

// Application-owned fused shader.
// This intentionally uses the D3D12Processing HLSL library instead of a single
// shortcut Processor class. It fuses:
//   NV12 -> RGB -> resize -> circular outside-region darken.

Texture2D<float>  YPlane  : register(t0);
Texture2D<float2> UVPlane : register(t1);
RWTexture2D<float4> Dst   : register(u0);

float3 LoadRgbAt(uint2 logicalSrc)
{
    uint2 srcPos = OffsetPosition(SrcX, SrcY, logicalSrc);
    float y = YPlane.Load(LoadCoord(srcPos));
    float2 uv = UVPlane.Load(LoadCoord(srcPos / 2));
    return DecodeYuv(y, uv, SrcRange, SrcMatrix);
}

float3 SamplePointYuv(uint2 dstLocal)
{
    float2 srcF = (float2(dstLocal) + 0.5f) * float2(ScaleX, ScaleY) - 0.5f;
    int2 ip = int2(round(srcF));
    ip = clamp(ip, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));
    return LoadRgbAt(uint2((uint)ip.x, (uint)ip.y));
}

float3 SampleLinearYuv(uint2 dstLocal)
{
    float2 srcF = (float2(dstLocal) + 0.5f) * float2(ScaleX, ScaleY) - 0.5f;
    float2 baseF = floor(srcF);
    float2 f = srcF - baseF;

    int2 p00 = int2(baseF);
    int2 p11 = p00 + int2(1, 1);
    p00 = clamp(p00, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));
    p11 = clamp(p11, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));

    float3 c00 = LoadRgbAt(uint2((uint)p00.x, (uint)p00.y));
    float3 c10 = LoadRgbAt(uint2((uint)p11.x, (uint)p00.y));
    float3 c01 = LoadRgbAt(uint2((uint)p00.x, (uint)p11.y));
    float3 c11 = LoadRgbAt(uint2((uint)p11.x, (uint)p11.y));

    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

float OutsideCircleAmount(uint2 dstLocal)
{
    float2 p = float2(dstLocal) + 0.5f;
    float2 center = float2((float)DstWidth, (float)DstHeight) * 0.5f;
    float radius = min((float)DstWidth, (float)DstHeight) * 0.35f;
    float feather = max(2.0f, min((float)DstWidth, (float)DstHeight) * 0.04f);
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
    uint2 p = tid.xy;
    if (!InRect(p, DstWidth, DstHeight)) {
        return;
    }

    float3 rgb = (Filter == PROCESSING_FILTER_POINT) ? SamplePointYuv(p) : SampleLinearYuv(p);
    rgb = ApplyOutsideDarken(p, rgb);

    Dst[OffsetPosition(DstX, DstY, p)] = FromLogicalRgba(saturate(float4(rgb, 1.0f)), DstFormat);
}
