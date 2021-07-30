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

// ÿ��������еĲ��ʽṹ��buffer,����һ������
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

// shader model 5.1��֧�ֵ���������,��Texture2DArray����������������,�����������������ߴ�͸�ʽ�и��ʸ��Բ���ͬ,������Եñ�Texture2DArrayҪ���
Texture2D gDiffuseMap[4] : register(t0);
// �ѽṹ��buffer����space1��,�Ӷ����������鲻��� �ṹ��������Դ ��������ص�
// !!ע��!! ��һ�е���������ռ�üĴ���t0,t1,t2,t3��space0�ռ�
// ��ʽָ��Ϊspace1�ռ�,����ʹ�üĴ���������ά��,��������Դ�ص�,֮ǰ������������ռ����to,t1,t2,t3��space0�ռ�,���ṹ��bufferռ����to��space1�ռ�
// ���������һ��,Ӧ�ô�t4��ʼ
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);// �ṹ�����


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
	float4x4 gTexTransform;
	uint gMaterialIndex;// ÿ��������еĶ�̬��������
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

	/// ������һ��: ��ÿ��������еĶ�̬���������������ṹ����ʵ�����
	MaterialData matData = gMaterialData[gMaterialIndex];// ��������еĽṹ�����
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
	/// ����: Ϊ�����β�ֵ�������������
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// �ֱ��ȡ������в��ʵ���������(��������,������ϵ��,�ֲڶ�,������������ȵ�)
    MaterialData matData = gMaterialData[gMaterialIndex]; // ��λ������еĽṹ�����
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;

	// ��ȫ����������gDiffuseMap �� ��̬�� ��������, Ȼ��������1���µ���������
	diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamLinearWrap, pin.TexC);
	
    // �Է��߲�ֵ�ᵼ����ǹ淶��,�����������¹淶��һ��
    pin.NormalW = normalize(pin.NormalW);

    // ��΢����1��ָ���۾�
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // �������� == �������� * �������Ļ������Դ
    float4 ambient = gAmbientLight*diffuseAlbedo;

    const float shininess = 1.0f - roughness;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    // ֱ�ӹ��ռ���
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // ������������ɫ == �������� + ֱ�ӹ���
    float4 litColor = ambient + directLight;

    // // ������ɫ�İ�����ֵ ������������ȡ
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


