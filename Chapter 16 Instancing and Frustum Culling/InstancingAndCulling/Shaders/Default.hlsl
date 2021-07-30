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

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

// 存有实例数据的结构化buffer
struct InstanceData
{
	float4x4 World;             // 单个实例中的世界矩阵
	float4x4 TexTransform;      // 单个实例中的纹理变换矩阵
	uint     MaterialIndex;     // 单个实例中的材质序数
	uint     InstPad0;          // 对齐
	uint     InstPad1;          // 对齐
	uint     InstPad2;          // 对齐
};
// 每个物体独有的材质结构化buffer,含有一堆属性
struct MaterialData
{
	float4   DiffuseAlbedo;
    float3   FresnelR0;
    float    Roughness;
	float4x4 MatTransform;
	uint     DiffuseMapIndex;
	uint     MatPad0;
	uint     MatPad1;
	uint     MatPad2;
};

// shader model 5.1才支持的纹理数组,与Texture2DArray类型数组区别在于,这种数组所存的纹理尺寸和格式有概率各自不相同,因而它显得比Texture2DArray要灵活
Texture2D gDiffuseMap[7] : register(t0);

// 上述含有7个元素的纹理数组占据了t0到t6寄存器的space0
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);// 关联实例数据的结构化buffer
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);// 关联材质数据的结构化buffer

// 一些采样器
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

/// !!注意物体CB转移到 实例结构体buffer中了

// 每趟绘制PASS里可能会发生改变的 CB
cbuffer cbPass : register(b0)
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
   
    Light gLights[MaxLights];
};

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
	
	// 使用nointerpolation修饰符, 让材质索引指向的都是未经插值的三角形
	nointerpolation uint MatIndex  : MATINDEX;
};

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout = (VertexOut)0.0f;
	
	/// 新增的一步,获取实例数据
	InstanceData instData = gInstanceData[instanceID];// 从结构化实例数组索引出 具体的实例
    // 暂存实例的属性们
	float4x4 world = instData.World;
	float4x4 texTransform = instData.TexTransform;// 从实例中取出纹理变换矩阵
	uint matIndex = instData.MaterialIndex;// 使用实例的材质索引 材质

	vout.MatIndex = matIndex;
	
	/// 新增的一步: 按每个物体独有的动态材质索引来索引结构体材质的数据
    MaterialData matData = gMaterialData[matIndex]; // 从结构化材质数组里索引出  每个物体独有的材质
	
    // 变换到世界空间
    float4 posW = mul(float4(vin.PosL, 1.0f), world);
    vout.PosW = posW.xyz;

    // 使用世界矩阵的逆转置矩阵计算法线
    vout.NormalW = mul(vin.NormalL, (float3x3)world);

    // 顶点被变换到齐次裁剪空间
    vout.PosH = mul(posW, gViewProj);
	
	// 为执行三角形插值而输出顶点属性
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);// 先用纹理坐标乘以 实例的纹理变换矩阵
	vout.TexC = mul(texC, matData.MatTransform).xy;               // 再乘以材质数据里的材质变换矩阵
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// 像素着色器取得 材质数据
	MaterialData matData = gMaterialData[pin.MatIndex];// 使用未经插值的的三角形顶点 材质
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;
	
	// 在全局纹理数组gDiffuseMap 里 动态地 查找纹理, 然后采样存成1个新的漫反照率
    diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamLinearWrap, pin.TexC);
	
    // 对法线插值会导致其非规范化,所以这里重新规范化一下
    pin.NormalW = normalize(pin.NormalW);

    // 从微表面1点指向眼睛
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // 环境光照 == 漫反照率 * 杂项常量里的环境光灯源
    float4 ambient = gAmbientLight*diffuseAlbedo;

    const float shininess = 1.0f - roughness;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // 点亮的最终颜色 == 环境光照 + 直接光照
    float4 litColor = ambient + directLight;

    // 点亮颜色的阿尔法值 从漫反照率里取
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


