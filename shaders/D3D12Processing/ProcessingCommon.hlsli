#ifndef D3D12_PROCESSING_COMMON_HLSLI
#define D3D12_PROCESSING_COMMON_HLSLI

#define THREAD_GROUP_X 16
#define THREAD_GROUP_Y 16

static const uint DXGI_FORMAT_R8G8B8A8_UNORM_VALUE = 28;
static const uint DXGI_FORMAT_B8G8R8A8_UNORM_VALUE = 87;
static const uint DXGI_FORMAT_NV12_VALUE = 103;

static const uint PROCESSING_FILTER_POINT = 0;
static const uint PROCESSING_FILTER_LINEAR = 1;
static const uint PROCESSING_MATRIX_IDENTITY = 0;
static const uint PROCESSING_MATRIX_BT601 = 1;
static const uint PROCESSING_MATRIX_BT709 = 2;
static const uint PROCESSING_MATRIX_BT2020 = 3;
static const uint PROCESSING_RANGE_FULL = 0;
static const uint PROCESSING_RANGE_LIMITED = 1;

cbuffer ProcessingConstants : register(b0)
{
    uint SrcWidth;
    uint SrcHeight;
    uint DstWidth;
    uint DstHeight;
    int SrcX;
    int SrcY;
    int DstX;
    int DstY;
    uint SrcFormat;
    uint DstFormat;
    uint SrcMatrix;
    uint SrcRange;
    uint DstMatrix;
    uint DstRange;
    uint Filter;
    uint AlphaMode;
    float ScaleX;
    float ScaleY;
    uint Reserved0;
    uint Reserved1;
};

bool InRect(uint2 p, uint w, uint h)
{
    return p.x < w && p.y < h;
}

uint2 OffsetPosition(int x, int y, uint2 p)
{
    return uint2((uint)x, (uint)y) + p;
}

int3 LoadCoord(uint2 p)
{
    return int3((int)p.x, (int)p.y, 0);
}

float4 ToLogicalRgba(float4 v, uint format)
{
    // HLSL typed texture load returns components in shader RGBA order.
    // DXGI_FORMAT_B8G8R8A8_UNORM describes memory layout; the SRV/UAV format
    // conversion already maps it to/from float4(R, G, B, A).  Do not manually
    // swizzle here, otherwise BGRA UAV writes become double-swizzled.
    (void)format;
    return v;
}

float4 FromLogicalRgba(float4 v, uint format)
{
    // See ToLogicalRgba.  The typed UAV store performs the BGRA memory layout
    // conversion for DXGI_FORMAT_B8G8R8A8_UNORM.
    (void)format;
    return v;
}

#endif
