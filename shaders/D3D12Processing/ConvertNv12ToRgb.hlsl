#include "ProcessingCommon.hlsli"
#include "ColorSpace.hlsli"

Texture2D<float> YPlane : register(t0);
Texture2D<float2> UVPlane : register(t1);
RWTexture2D<float4> Dst : register(u0);

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 p = tid.xy;
    if (!InRect(p, DstWidth, DstHeight)) {
        return;
    }

    uint2 srcPos = OffsetPosition(SrcX, SrcY, p);
    uint2 dstPos = OffsetPosition(DstX, DstY, p);
    float y = YPlane.Load(LoadCoord(srcPos));
    float2 uv = UVPlane.Load(LoadCoord(srcPos / 2));
    float3 rgb = DecodeYuv(y, uv, SrcRange, SrcMatrix);
    Dst[dstPos] = FromLogicalRgba(float4(rgb, 1.0), DstFormat);
}
