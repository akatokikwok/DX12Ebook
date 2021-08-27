//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// 暂存结构化材质的所有属性(漫反射率、菲涅尔系数、粗糙度、2D漫反射贴图序数)
	MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
    uint diffuseTexIndex = matData.DiffuseMapIndex;// 2D漫反射贴图序数

	// 在2D纹理数组里使用各向异性过滤采样出 普通的2D贴图颜色 并和 原漫反射率叠加
	diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	
    // 像素着色器阶段 规范化法线； 对法线插值会导致其非规范化,所以这里重新规范化一下
    pin.NormalW = normalize(pin.NormalW);

    // 从微表面1点指向眼睛(规范化)
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // ambient == 漫反照率 * 杂项常量里的环境光灯源
    float4 ambient = gAmbientLight*diffuseAlbedo;
    // 直接光照
    const float shininess = 1.0f - roughness;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // 点亮的最终颜色 == 环境光照 + 直接漫反射光照
    float4 litColor = ambient + directLight;

	// 之前已经将CubeMap数据传入，我们要做反射，只需在shader中采样CubeMap计算得到环境反射颜色
    // 再计算出菲涅尔高光，最后将它们混合。可以看到，我们乘以一个roughness的取反，用来控制环境色强度，越光滑环境反射越强烈，反之越弱。
	// 环境反射高光
    float3 r = reflect(-toEyeW, pin.NormalW);                           // 看向微表面的反射向量
	float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);        // 采样立方体贴图来得到环境高光
	float3 fresnelFactor = SchlickFresnel(fresnelR0, pin.NormalW, r);   // 使用SchlickFresnel暂存一个菲涅尔高光因子
    // 最终光照等于 环境光照 + 直接漫反射光照 + 所有的反射光照
    // 利用菲涅尔高光因子计算出菲涅尔高光颜色 混合叠加 环境高光颜色 
	litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;

    // 点亮颜色的阿尔法值 从漫反照率里取
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


