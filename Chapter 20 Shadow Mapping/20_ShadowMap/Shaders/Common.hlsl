//***************************************************************************************
// Common.hlsl by Frank Luna (C) 2015 All Rights Reserved.
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

struct MaterialData
{
	float4   DiffuseAlbedo;
	float3   FresnelR0;
	float    Roughness;
	float4x4 MatTransform;
	uint     DiffuseMapIndex;
	uint     NormalMapIndex;
	uint     MatPad1;
	uint     MatPad2;
};

TextureCube gCubeMap : register(t0);
Texture2D gShadowMap : register(t1);// 阴影图(也就是场景的深度图)

// An array of textures, which is only supported in shader model 5.1+.  Unlike Texture2DArray, the textures
// in this array can be different sizes and formats, making it more flexible than texture arrays.
Texture2D gTextureMaps[10] : register(t2);

// Put in space1, so the texture array does not overlap with these resources.  
// The texture array will occupy registers t0, t1, ..., t3 in space0. 
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6); // 做PCF的采样需要使用“比较采样器”，这使硬件能够执行阴影图的比较测试，且需要在过滤采样结果之前完成

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld;
	float4x4 gTexTransform;
	uint gMaterialIndex;
	uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
};

cbuffer cbPass : register(b1)
{
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float4x4 gShadowTransform;
	float3 gEyePosW;
	float cbPerObjectPad1;
	float2 gRenderTargetSize;
	float2 gInvRenderTargetSize;
	float gNearZ;
	float gFarZ;
	float gTotalTime;
	float gDeltaTime;
	float4 gAmbientLight;

	// Indices [0, NUM_DIR_LIGHTS) are directional lights;
	// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
	// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
	// are spot lights for a maximum of MaxLights per object.
	Light gLights[MaxLights];
};

//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
	float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
	float3 N = unitNormalW;
	float3 T = normalize(tangentW - dot(tangentW, N) * N);
	float3 B = cross(N, T);

	float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
	float3 bumpedNormalW = mul(normalT, TBN);

	return bumpedNormalW;
}

/// 为阴影图构建PCF,返回阴影因子
float CalcShadowFactor(float4 shadowPosH/*接受外部1个顶点*/)
{
	// 将处于齐次裁剪空间未执行齐次除法的顶点变换到NDC空间（如果是正交投影，则W=1）
	shadowPosH.xyz /= shadowPosH.w;

	// 获取 处于NDC空间中的深度值
	float depth = shadowPosH.z;

	/* 读取阴影图--gShadowMap的宽高及mip级数*/
	uint width, height, numMips;
	gShadowMap.GetDimensions(0, width, height, numMips);

	// 纹素尺寸
	float dx = 1.0f / (float)width;

	/* 单次PCF预存值*/
	float percentLit = 0.0f;
	/* 使用9核, 3排3列*/
	const float2 offsets[9] =
	{
		float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
		float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
		float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
	};

	/* PCF（类似均值模糊算法）执行9次4-tap PCF*/
	[unroll]
	for (int i = 0; i < 9; ++i)
	{
		// 每个核都执行tap4 PCF计算, DX12可以调用SampleCmpLevelZero函数来执行PCF，并最大程度的优化采样过程。
		// 做PCF的采样需要使用“比较采样器”，这使硬件能够执行阴影图的比较测试，且需要在过滤采样结果之前完成
		percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,shadowPosH.xy + offsets[i], depth).r;
		
		// 单独测试一下PCF，看下软硬阴影的区别。将PCF计算中累加的offsets数组改成4号元素，即中间的0号，这样相当于没有做PCF,会发现阴影边缘有明显的锯齿
		// percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,shadowPosH.xy + offsets[4], depth).r;
	}
	
	/* 将9次PCF取均值*/
	return percentLit / 9.0f;
}

