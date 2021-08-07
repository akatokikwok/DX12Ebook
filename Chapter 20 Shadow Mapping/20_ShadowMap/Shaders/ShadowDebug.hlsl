//***************************************************************************************
// ShadowDebug.hlsl:我们需要单独一个shader来将采样阴影图并将其渲染到面片上，所以新建一个DebugShader
//***************************************************************************************

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// 此时已经处于齐次裁剪空间了
	vout.PosH = float4(vin.PosL, 1.0f);

	vout.TexC = vin.TexC;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return float4(gShadowMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f); // 在shader中线性采样阴影图，并返回最终颜色
}


