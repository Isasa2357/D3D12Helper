//
// Grayscale.hlsl
//
// 02_ComputeGrayscale サンプルが使う Compute Shader の参照コピー。
// 実際のサンプルはこのソースを文字列として埋め込み、DXC でランタイムコンパイルする
// （CompileShaderFromSource_Dxc, target = "cs_6_0"）。
//
// 事前コンパイルして使いたい場合は、例えば次のように .cso を作り、
//   dxc -T cs_6_0 -E main Grayscale.hlsl -Fo Grayscale.cso
// LoadShaderBytecodeFromFile("Grayscale.cso") で読み込めばよい。
//
// Root Signature はテンプレート生成（SRV1 + UAV1 + Root定数2）に対応する:
//   t0 = 入力 SRV, u0 = 出力 UAV, b0 = { width, height }
//

Texture2D<float4>   gInput  : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Constants : register(b0)
{
    uint gWidth;
    uint gHeight;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    float4 c = gInput.Load(int3(id.xy, 0));

    // Rec.601 輝度係数でグレースケール化
    float l = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));

    gOutput[id.xy] = float4(l, l, l, c.a);
}
