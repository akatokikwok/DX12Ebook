//***************************************************************************************
// ShadowDebug.hlsl 我们需要单独一个shader来将采样阴影图并将其渲染到面片上，所以新建一个DebugShader
//***************************************************************************************

#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    // 此时顶点着色器里的顶点已经处于齐次裁剪空间
    vout.PosH = float4(vin.PosL, 1.0f);
	
    vout.TexC = vin.TexC;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // 以输出顶点的纹理坐标为查找依据, 采样SSAO图的rrr,并得到最终绘制在屏幕某侧面片上的颜色
    return float4(gSsaoMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);
}


