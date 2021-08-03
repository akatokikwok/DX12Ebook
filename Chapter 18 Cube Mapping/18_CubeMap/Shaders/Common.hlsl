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
	uint     MatPad0;
	uint     MatPad1;
	uint     MatPad2;
};

TextureCube gCubeMap : register(t0); // 立方体纹理使用 TextureCube关键字表示

//一种只有SM5.1才支持的纹理数组
//与Texture2DArray类型数组不同的是，此数组中所存纹理的尺寸和格式可各不相同
//这使它比一般纹理数组更为灵活
Texture2D gDiffuseMap[4] : register(t1); //所有2D漫反射贴图

//材质数据的结构化缓冲区，使用t0的space1空间
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);

//6个不同类型的采样器
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);// 使用线性采样
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform; //UV顶点变换矩阵
	uint gMaterialIndex;
	uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
};

// Constant data that varies per material.
cbuffer cbPass : register(b1)
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

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};


