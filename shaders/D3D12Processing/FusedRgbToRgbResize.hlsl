#include "ProcessingCommon.hlsli"

Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);

float4 LoadLogical(uint2 p)
{
    float4 c = Src.Load(LoadCoord(p));
    return ToLogicalRgba(c, SrcFormat);
}

float4 SamplePoint(uint2 p)
{
    float2 srcF = (float2(p) + 0.5) * float2(ScaleX, ScaleY) - 0.5;
    int2 ip = int2(round(srcF));
    ip = clamp(ip, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));
    return LoadLogical(OffsetPosition(SrcX, SrcY, uint2((uint)ip.x, (uint)ip.y)));
}

float4 SampleLinear(uint2 p)
{
    float2 srcF = (float2(p) + 0.5) * float2(ScaleX, ScaleY) - 0.5;
    float2 baseF = floor(srcF);
    float2 f = srcF - baseF;
    int2 p0 = int2(baseF);
    int2 p1 = p0 + int2(1, 1);
    p0 = clamp(p0, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));
    p1 = clamp(p1, int2(0, 0), int2(int(SrcWidth) - 1, int(SrcHeight) - 1));

    float4 c00 = LoadLogical(OffsetPosition(SrcX, SrcY, uint2((uint)p0.x, (uint)p0.y)));
    float4 c10 = LoadLogical(OffsetPosition(SrcX, SrcY, uint2((uint)p1.x, (uint)p0.y)));
    float4 c01 = LoadLogical(OffsetPosition(SrcX, SrcY, uint2((uint)p0.x, (uint)p1.y)));
    float4 c11 = LoadLogical(OffsetPosition(SrcX, SrcY, uint2((uint)p1.x, (uint)p1.y)));
    float4 cx0 = lerp(c00, c10, f.x);
    float4 cx1 = lerp(c01, c11, f.x);
    return lerp(cx0, cx1, f.y);
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 p = tid.xy;
    if (!InRect(p, DstWidth, DstHeight)) {
        return;
    }

    float4 c = (Filter == PROCESSING_FILTER_POINT) ? SamplePoint(p) : SampleLinear(p);
    uint2 dstPos = OffsetPosition(DstX, DstY, p);
    Dst[dstPos] = FromLogicalRgba(saturate(c), DstFormat);
}
