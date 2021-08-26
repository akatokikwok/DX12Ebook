//=============================================================================
// Sky.hlsl 总是以摄像机作为天空球的中心， 摄像机动，
// 天空球也跟着动,这样摄像机永远不会移到天空球外,造成穿帮
// 立方体贴图可以直接使用Sample函数, 只是第二个参数是三维向量， 即查找向量.
//=============================================================================

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

/* 天空球所需要的顶点实例*/
struct VertexOut
{
    float4 PosH : SV_POSITION; // CubeMap的每顶点"齐次裁剪空间位置PosH"
    float3 PosL : POSITION; // CubeMap的每个顶点的"查找向量PosL"
};
 
VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// CubeMap的查找向量是PosL; 输出顶点里仍然保留POSL这个查找向量
    vout.PosL = vin.PosL;
	
	// 将顶点变换到世界坐标
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

	// 总是以摄像机作为天空球的中心
    posW.xyz += gEyePosW;

	// 将顶点变换到齐次裁剪空间,注意这里比较特殊是取xyww
    vout.PosH = mul(posW, gViewProj).xyww; // 注意这里比较特殊是取xyww
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // 线性采样CubeMap纹理
    return gCubeMap.Sample(gsamLinearWrap, pin.PosL/*至于参数"位置"则使用立方体纹理的查找向量*/);
}

