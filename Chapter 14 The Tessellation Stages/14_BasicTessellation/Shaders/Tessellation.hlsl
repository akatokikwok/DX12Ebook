//***************************************************************************************
// Tessellation.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************


// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D gDiffuseMap : register(t0);


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
    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VertexIn
{
    float3 PosL : POSITION;
};

struct VertexOut
{
    float3 PosL : POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
    vout.PosL = vin.PosL;

    return vout;
}
 
struct PatchTess// ��Ƭ(patch)��ϸ��
{
    float EdgeTess[4]   : SV_TessFactor;      // quad patch�ı�Ե,4�����Ƶ�
    float InsideTess[2] : SV_InsideTessFactor;// quad patch���ڲ�,2�����Ƶ�
    
    // �����ٸ�ÿ����Ƭ��������Ķ�����Ϣ
};

// ����hull shader������� tesselation factor
PatchTess ConstantHS(InputPatch<VertexOut, 4> patch, /*����hull shader����Ƭ���еĿ��Ƶ�Ϊ����*/
                     uint patchID : SV_PrimitiveID   /*��ƬIDֵ*/
)
{
    PatchTess pt;// ����1��patch
	
    float3 centerL = 0.25f * (patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL);
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;
	
    float d = distance(centerW, gEyePosW);// ���񵽹۲��ľ���

    // ����������۲��ľ���������Ƭִ����Ƕ����,����d>=dl(ִ����Ƕ����Զ����),����Ƕ������Ϊ0;
    // ����d<=d0(ִ����Ƕ���������),����Ƕ������Ϊ64;
    // [d0, dl]������ִ����Ƕ������һ����������
	
    const float d0 = 20.0f;
    const float d1 = 100.0f;
    float tess = 64.0f * saturate((d1 - d) / (d1 - d0));

	// ���ı�����Ƭ�ĸ�����(��Ե,�ڲ�)����ͳһ����Ƕ������

    pt.EdgeTess[0] = tess;// �ı�����Ƭ������Ե
    pt.EdgeTess[1] = tess;// �ϲ��Ե
    pt.EdgeTess[2] = tess;// �Ҳ��Ե
    pt.EdgeTess[3] = tess;// �²��Ե
	
    pt.InsideTess[0] = tess;// u��(�ı���patch�ڲ�ϸ�ֵ���)
    pt.InsideTess[1] = tess;// v��(�ı���patch�ڲ�ϸ�ֵ���)
    return pt;
}

struct HullOut
{
    float3 PosL : POSITION;
};

// ����һ�����Ƶ������ɫ��,�����н��䵱�򵥵Ĵ�����ɫ��,���޸��κο��Ƶ�
[domain("quad")]/*��Ƭ������,��ѡ�ùؼ����� tri��quad��isoline*/
[partitioning("integer")]/*ָ������ϸ�ֵ�ϸ��ģʽ, ��interger��fractional_even*/
[outputtopology("triangle_cw")]/*ϸ��������������������*/
[outputcontrolpoints(4)]/*hull shaderִ�еĴ���*/
[patchconstantfunc("ConstantHS")]/*ָ�����������ɫ���ĺ��������ֶ�*/
[maxtessfactor(64.0f)]/*֪ͨ��������ʹ�õ�����ϸ�����ӵ����ֵ*/
HullOut HS(InputPatch<VertexOut, 4> p,/*ͨ��inputPatch�ؼ��ֿ��԰�patch���еĿ��Ƶ㶼���ݵ�hull shader֮��*/
           uint i : SV_OutputControlPointID, /*ϵͳֵSV_OutputControlPointID������Щ���ڱ�hull shader��ǰ���ڹ��������control point*/
           uint patchId : SV_PrimitiveID /*��ƬIDֵ*/
)
{
    HullOut hout;
	
    hout.PosL = p[i].PosL;
	
    return hout;
}

/// ��������Ƕ,ϸ�ֵ������ν���������ϸ�ֵ�patch֮��,ϸ����Ȼ�����ḻ;
/// ���,Ҫ��ĳ�ַ�ʽ�ƶ���Щ�����ĵ�, ������������; ��ô��Щ�����Ͷ���������ɫ��ִ�е�

struct DomainOut// ����ɫ�����ֵ
{
    float4 PosH : SV_POSITION;
};

// ����˫���Բ�ֵʵ��DS
// ÿ����Ƕ������(tesselator)���������ʱ��ͻ��������ɫ��
// ���Խ�����Ϊ��Ƕ�׶δ���֮���"vertex shader"
[domain("quad")]
DomainOut DS(PatchTess patchTess,/*��һ����Ƭ*/
             float2 uv : SV_DomainLocation, /*����ɫ�������Ĳ���ʵ�ʶ���λ��,����λ��patch domain space�ڵĲ�������(u,v)*/
             const OutputPatch<HullOut, 4> quad/*����ǿ��Ƶ������ɫ���Ŀ��Ƶ����ֵ*/
)
{
    DomainOut dout;
	
	// ˫���Բ�ֵ����õ�һ�������ĵ� p
    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
    float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.x);
    float3 p = lerp(v1, v2, uv.y);
	
    /// ��������Ƕ,ϸ�ֵ������ν���������ϸ�ֵ�patch֮��,ϸ����Ȼ�����ḻ;
    /// ���,Ҫ��ĳ�ַ�ʽ�ƶ���Щ�����ĵ�, ������������; ��ô��Щ�����Ͷ���������ɫ��ִ�е�
	// λ����ͼ, ��ģ�⺯����y���϶����ִ��ƫ��
    p.y = 0.3f * (p.z * sin(p.x) + p.x * cos(p.z));
	
    float4 posW = mul(float4(p, 1.0f), gWorld);// �ѵ�p�任������ռ�
    dout.PosH = mul(posW, gViewProj);//�任����βü��ռ�
	
    return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
