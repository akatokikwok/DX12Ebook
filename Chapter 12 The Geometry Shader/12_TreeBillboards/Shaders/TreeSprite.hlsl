//***************************************************************************************
// TreeSprite.hlsl by Frank Luna (C) 2015 All Rights Reserved.
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

// ����4������Ԫ�ص���������,ÿ������Ԫ���ж����ж��ص���ľ����
Texture2DArray gTreeMapArray : register(t0); // ����ʹ��ר��������,��������

// һЩ������
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
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

    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights]; // ��Դ��������PASS CB��
};

// ÿ�ֲ������ֵĳ���
cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};
 
struct VertexIn
{
    float3 PosW : POSITION;
    float2 SizeW : SIZE; // ����й����ƵĿ�͸�
};

struct VertexOut
{
    float3 CenterW : POSITION; // ���������ĵ�
    float2 SizeW : SIZE;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    uint PrimID : SV_PrimitiveID;
};

/// ����shader
VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// ����ڶ���shader�� ֱ�Ӱ����ݴ��� ����shader
    vout.CenterW = vin.PosW;
    vout.SizeW = vin.SizeW;

    return vout;
}
 
// ����ɫ�������ǰѹ������м��1������չΪ1���ı���,��ʹ֮��y�������ʼ�������۾�
// ����﷨��ʾ����shader����֮ǰ�����vertex����; 4�Ǽ���shader���ε��������������������
// ����ı�������Ϊ �������ͽṹ���б������� * maxvertexcount
[maxvertexcount(4)]
void GS(point VertexOut gin[1],
        uint primID : SV_PrimitiveID, // ��ָ��SV_PrimitiveID����,��������װ��׶λ�Ϊÿ��ͼԪ����ͼԪID��0~n-1,������ɫ������ͼԪIDд������Ķ�����,�Դ���Ϊһ����Ϣ������������ɫ���׶�,��������ɫ�����ͼԪID�����������������
  inout TriangleStream<GeoOut> triStream // �����������Ҫ��inout���η�,�ұ�����������
  // ���������������ζ���,����shader����Ķ�ӦͼԪ�ض�������strip��������strip,���������б���������б�ɽ���ResstartStripʵ��
)
{
	//
	// �����������������ľֲ�����ռ䣬ʹ�������y����룬�����۾�.
	
	// �μ�P409��ͼ
    float3 up = float3(0.0f, 1.0f, 0.0f); // �������ı�����y�����ϵ�λ����
    float3 look = gEyePosW - gin[0].CenterW; // �ӹ������ı��μ�������ָ���۾�������
    look.y = 0.0f; // y�����,ͶӰ��xzƽ��
    look = normalize(look); // �淶��
    float3 right = cross(up, look); // ��look��up����õ�right

	//
	// Compute triangle strip vertices (quad) in world space.���������δ�������ռ��4������(���Ǹ�����չ�����ı���)
	//
    float halfWidth = 0.5f * gin[0].SizeW.x;
    float halfHeight = 0.5f * gin[0].SizeW.y;
    float4 v[4];
    v[0] = float4(gin[0].CenterW + halfWidth * right - halfHeight * up, 1.0f);
    v[1] = float4(gin[0].CenterW + halfWidth * right + halfHeight * up, 1.0f);
    v[2] = float4(gin[0].CenterW - halfWidth * right - halfHeight * up, 1.0f);
    v[3] = float4(gin[0].CenterW - halfWidth * right + halfHeight * up, 1.0f);

	//
	// ���ı��εĶ���Ӿֲ�����ϵ�任������ռ���,���������������δ� ģʽ���
	//
	
    float2 texC[4] = // ��һ���4��UV
    {
        float2(0.0f, 1.0f),
		float2(0.0f, 0.0f),
		float2(1.0f, 1.0f),
		float2(1.0f, 0.0f)
    };
	
    GeoOut gout;
	[unroll]
    for (int i = 0; i < 4; ++i)
    {
        gout.PosH = mul(v[i], gViewProj);
        gout.PosW = v[i].xyz;
        gout.NormalW = look;
        gout.TexC = texC[i];
        gout.PrimID = primID; // ������ɫ������ͼԪIDд������Ķ�����,�Դ���Ϊһ����Ϣ������������ɫ���׶�,��������ɫ�����ͼԪID�����������������
		
        triStream.Append(gout); // ʹ��Append���÷�����������б� ��ӵ�������
    }
}

/// ������ɫ��
float4 PS(GeoOut pin) : SV_Target
{
    // ��������ʹ��֮ǰGS�׶δ�������ͼԪID��Ϣ,���������������������; ��3�������������������;
    // ʹ������������,��������������ͻ��Ƶ��ö����ٵ�ֻ��һ��
    float3 uvw = float3(pin.TexC, pin.PrimID % 3);
    // ����������ִ�в��� ��ȡ�����ص��������� ���� ���ʳ�����ķ����� ���
    float4 diffuseAlbedo = gTreeMapArray.Sample(gsamAnisotropicWrap, uvw) * gDiffuseAlbedo;
	
#ifdef ALPHA_TEST
	// ������������ֵС��0.1������,Ҫ��������,�Ա�����˳���ɫ��;����Щ����Ƭ���޳���
	clip(diffuseAlbedo.a - 0.1f);
#endif

    // �Է��߲�ֵ�ᵼ����ǹ淶��,�����������¹淶��һ��
    pin.NormalW = normalize(pin.NormalW);

    // ��΢����1��ָ���۾�
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    // �������� == �������� * �������Ļ������Դ
    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    // ֱ�ӹ��ռ���
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // ������������ɫ == �������� + ֱ�ӹ���
    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - gFogStart) / gFogRange);
	litColor = lerp(litColor, gFogColor, fogAmount);
#endif

    // ������ɫ�İ�����ֵ ������������ȡ
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


