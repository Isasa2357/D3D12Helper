#include "ProcessingCommon.hlsli"
#include "ColorSpace.hlsli"

Texture2D<float4> Src : register(t0);
RWTexture2D<float> YPlane : register(u0);
RWTexture2D<float2> UVPlane : register(u1);

float3 LoadLogicalRgb(uint2 srcPos)
{
    float4 c = Src.Load(LoadCoord(srcPos));
    c = ToLogicalRgba(c, SrcFormat);
    return saturate(c.rgb);
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 p = tid.xy;

    if (p.x < DstWidth && p.y < DstHeight) {
        uint2 srcPos = OffsetPosition(SrcX, SrcY, p);
        uint2 dstPos = OffsetPosition(DstX, DstY, p);
        float3 rgb = LoadLogicalRgb(srcPos);
        float3 yuv = EncodeYuv(rgb, DstRange, DstMatrix);
        YPlane[dstPos] = yuv.x;
    }

    if (p.x < (DstWidth / 2) && p.y < (DstHeight / 2)) {
        uint2 baseSrc = OffsetPosition(SrcX, SrcY, p * 2);
        float3 rgb00 = LoadLogicalRgb(baseSrc + uint2(0, 0));
        float3 rgb10 = LoadLogicalRgb(baseSrc + uint2(1, 0));
        float3 rgb01 = LoadLogicalRgb(baseSrc + uint2(0, 1));
        float3 rgb11 = LoadLogicalRgb(baseSrc + uint2(1, 1));
        float3 rgb = (rgb00 + rgb10 + rgb01 + rgb11) * 0.25;
        float3 yuv = EncodeYuv(rgb, DstRange, DstMatrix);
        uint2 uvDst = (uint2((uint)DstX, (uint)DstY) / 2) + p;
        UVPlane[uvDst] = yuv.yz;
    }
}
