//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Default shader, currently supports lighting.
//***************************************************************************************

// 光源的一些参数默认设置
/* 索引[0, NUM_DIR_LIGHTS]表示平行光源 
 * 索引[NUM_DIR_LIGHTS, NUM_DIR_LIGHTS + NUM_POINT_LIGHTS]表示点光源
 * 索引[ NUM_DIR_LIGHTS + NUM_POINT_LIGHTS,  NUM_DIR_LIGHTS + NUM_POINT_LIGHTS+NUM_SPOT_LIGHTS]表示聚光灯源
 */
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// 
#include "LightingUtil.hlsl"

// 下面是每帧都会发生变化的常量数据

cbuffer cbPerObject : register(b0)// 世界矩阵的,位于槽位0
{
    float4x4 gWorld;
};

cbuffer cbMaterial : register(b1)// 材质的,位于槽位1
{
	float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float  gRoughness;
	float4x4 gMatTransform;
};

cbuffer cbPass : register(b2)// 渲染过程的,位于槽位2
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    
    Light gLights[MaxLights];// 光源数量不超过16个
};
 
struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	
    // 把顶点变换到世界空间
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // 关于法线,假设这里执行的是等比缩放,否则这里需要使用 world的逆转置矩阵
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    // 把顶点变换到齐次裁剪空间
    vout.PosH = mul(posW, gViewProj);

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // 在pixel shader里,对法线插值有概率会导致其非规范化,所以这里要规范化法线
    pin.NormalW = normalize(pin.NormalW);

    // 微表面1点指向眼睛的向量(规范化)
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

	// 间接光照:环境光
    float4 ambient = gAmbientLight*gDiffuseAlbedo;// render pass的环境光 * 材质里的漫反射系数

    // 直接光照计算如下:
    const float shininess = 1.0f - gRoughness; // 定义光滑度 == 1 - 粗糙度
    Material mat = { gDiffuseAlbedo, gFresnelR0, shininess };// 构建一张材质
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor
    );

    // 点亮的颜色就是 环境光 + 直接光照
    float4 litColor = ambient + directLight;

    // 点亮颜色的alpha值由 漫反射材质决定
    litColor.a = gDiffuseAlbedo.a;

    return litColor;
}


