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

// ����ʵ�����ݵĽṹ��buffer
struct InstanceData
{
	float4x4 World;             // ����ʵ���е��������
	float4x4 TexTransform;      // ����ʵ���е�����任����
	uint     MaterialIndex;     // ����ʵ���еĲ�������
	uint     InstPad0;          // ����
	uint     InstPad1;          // ����
	uint     InstPad2;          // ����
};
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
Texture2D gDiffuseMap[7] : register(t0);

// ��������7��Ԫ�ص���������ռ����t0��t6�Ĵ�����space0
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);// ����ʵ�����ݵĽṹ��buffer
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);// �����������ݵĽṹ��buffer

// һЩ������
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

/// !!ע������CBת�Ƶ� ʵ���ṹ��buffer����

// ÿ�˻���PASS����ܻᷢ���ı�� CB
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
	
	// ʹ��nointerpolation���η�, �ò�������ָ��Ķ���δ����ֵ��������
	nointerpolation uint MatIndex  : MATINDEX;
};

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout = (VertexOut)0.0f;
	
	/// ������һ��,��ȡʵ������
	InstanceData instData = gInstanceData[instanceID];// �ӽṹ��ʵ������������ �����ʵ��
    // �ݴ�ʵ����������
	float4x4 world = instData.World;
	float4x4 texTransform = instData.TexTransform;// ��ʵ����ȡ������任����
	uint matIndex = instData.MaterialIndex;// ʹ��ʵ���Ĳ������� ����

	vout.MatIndex = matIndex;
	
	/// ������һ��: ��ÿ��������еĶ�̬���������������ṹ����ʵ�����
    MaterialData matData = gMaterialData[matIndex]; // �ӽṹ������������������  ÿ��������еĲ���
	
    // �任������ռ�
    float4 posW = mul(float4(vin.PosL, 1.0f), world);
    vout.PosW = posW.xyz;

    // ʹ������������ת�þ�����㷨��
    vout.NormalW = mul(vin.NormalL, (float3x3)world);

    // ���㱻�任����βü��ռ�
    vout.PosH = mul(posW, gViewProj);
	
	// Ϊִ�������β�ֵ�������������
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);// ��������������� ʵ��������任����
	vout.TexC = mul(texC, matData.MatTransform).xy;               // �ٳ��Բ���������Ĳ��ʱ任����
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// ������ɫ��ȡ�� ��������
	MaterialData matData = gMaterialData[pin.MatIndex];// ʹ��δ����ֵ�ĵ������ζ��� ����
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
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // ������������ɫ == �������� + ֱ�ӹ���
    float4 litColor = ambient + directLight;

    // ������ɫ�İ�����ֵ ������������ȡ
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


