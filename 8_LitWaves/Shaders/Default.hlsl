//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Default shader, currently supports lighting.
//***************************************************************************************

// ��Դ��һЩ����Ĭ������
/* ����[0, NUM_DIR_LIGHTS]��ʾƽ�й�Դ 
 * ����[NUM_DIR_LIGHTS, NUM_DIR_LIGHTS + NUM_POINT_LIGHTS]��ʾ���Դ
 * ����[ NUM_DIR_LIGHTS + NUM_POINT_LIGHTS,  NUM_DIR_LIGHTS + NUM_POINT_LIGHTS+NUM_SPOT_LIGHTS]��ʾ�۹��Դ
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

// ������ÿ֡���ᷢ���仯�ĳ�������

cbuffer cbPerObject : register(b0)// ��������,λ�ڲ�λ0
{
    float4x4 gWorld;
};

cbuffer cbMaterial : register(b1)// ���ʵ�,λ�ڲ�λ1
{
	float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float  gRoughness;
	float4x4 gMatTransform;
};

cbuffer cbPass : register(b2)// ��Ⱦ���̵�,λ�ڲ�λ2
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
    
    Light gLights[MaxLights];// ��Դ����������16��
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
	
    // �Ѷ���任������ռ�
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // ���ڷ���,��������ִ�е��ǵȱ�����,����������Ҫʹ�� world����ת�þ���
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    // �Ѷ���任����βü��ռ�
    vout.PosH = mul(posW, gViewProj);

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // ��pixel shader��,�Է��߲�ֵ�и��ʻᵼ����ǹ淶��,��������Ҫ�淶������
    pin.NormalW = normalize(pin.NormalW);

    // ΢����1��ָ���۾�������(�淶��)
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

	// ��ӹ���:������
    float4 ambient = gAmbientLight*gDiffuseAlbedo;// render pass�Ļ����� * �������������ϵ��

    // ֱ�ӹ��ռ�������:
    const float shininess = 1.0f - gRoughness; // ����⻬�� == 1 - �ֲڶ�
    Material mat = { gDiffuseAlbedo, gFresnelR0, shininess };// ����һ�Ų���
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor
    );

    // ��������ɫ���� ������ + ֱ�ӹ���
    float4 litColor = ambient + directLight;

    // ������ɫ��alphaֵ�� ��������ʾ���
    litColor.a = gDiffuseAlbedo.a;

    return litColor;
}


