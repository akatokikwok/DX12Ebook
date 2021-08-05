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
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;// 主shader中声明CPU传入数据，增加Tangent字段
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;// 主shader中声明CPU传入数据，增加Tangent字段
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

	// Fetch the material data.
    MaterialData matData = gMaterialData[gMaterialIndex];
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // 只做均匀缩放，所以可以不使用逆转置矩阵
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
	// 将顶点切线从物体空间转至世界空间
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;
	
    return vout;
}

/// 像素着色器中，将采样法线图后得到的像素法线从TS变换到WS，并使用这个法线参与光照计算
float4 PS(VertexOut pin) : SV_Target
{
	// 暂存结构化材质的所有属性(漫反射率、菲涅尔系数、粗糙度、2D漫反射贴图序数、2D法线贴图序数)
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
	
	// 像素着色器阶段 规范化法线； 对法线插值会导致其非规范化,所以这里重新规范化一下
    pin.NormalW = normalize(pin.NormalW);
	
    // 先采样法线纹理得到法线值
    // 再使用接口把法线从切空间变换到世界空间
    float4 normalMapSample = gTextureMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalW, pin.TangentW);// 变换到世界空间后的法线

	// 在纹理数组中动态地查找漫反射纹理
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

    // 从微表面1点指向眼睛(规范化)
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // 间接光照 == 漫反照率 * 杂项常量里的环境光灯源
    float4 ambient = gAmbientLight * diffuseAlbedo;
    // 直接光照
    const float shininess = (1.0f - roughness) * normalMapSample.a;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        bumpedNormalW, /*这里替换为"从切空间变换到世界空间的法线",而不再是前一节的pin.NormalW*/
        toEyeW, shadowFactor);
    // 点亮颜色 == 环境光照 + 直接漫反射光照
    float4 litColor = ambient + directLight;

	// 环境反射
    float3 r = reflect(-toEyeW, bumpedNormalW);
    float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);       // 采样立方体贴图来得到环境高光
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);// 使用SchlickFresnel暂存一个菲涅尔高光因子
    // 最终光照等于 环境光照 + 直接漫反射光照 + 所有的镜面反射光照(即在原有基础上再叠一个镜面光)
    // 利用菲涅尔高光因子计算出菲涅尔高光颜色 混合叠加 环境高光颜色
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;
	
    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


