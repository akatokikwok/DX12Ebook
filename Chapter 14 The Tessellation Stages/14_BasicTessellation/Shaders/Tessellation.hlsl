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
 
struct PatchTess// 面片(patch)的细节
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
    
    // 允许再给每个面片附加所需的额外信息
};

// 常量hull shader必须输出 tesselation factor
PatchTess ConstantHS(InputPatch<VertexOut, 4> patch, /*常量hull shader以面片所有的控制点为输入*/
                     uint patchID : SV_PrimitiveID   /*面片ID值*/
)
{
    PatchTess pt;// 声明1个patch
	
    float3 centerL = 0.25f * (patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL);
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;
	
    float d = distance(centerW, gEyePosW);

	// Tessellate the patch based on distance from the eye such that
	// the tessellation is 0 if d >= d1 and 64 if d <= d0.  The interval
	// [d0, d1] defines the range we tessellate in.
	
    const float d0 = 20.0f;
    const float d1 = 100.0f;
    float tess = 64.0f * saturate((d1 - d) / (d1 - d0));

	// 将该patch从各方面均匀地镶嵌为三等分

    pt.EdgeTess[0] = tess;// 四边形面片的左侧边缘
    pt.EdgeTess[1] = tess;// 上侧边缘
    pt.EdgeTess[2] = tess;// 右侧边缘
    pt.EdgeTess[3] = tess;// 下侧边缘
	
    pt.InsideTess[0] = tess;// u轴(四边形patch内部细分的列)
    pt.InsideTess[1] = tess;// v轴(四边形patch内部细分的行)
    return pt;
}

struct HullOut
{
    float3 PosL : POSITION;
};

// 这是一个控制点外壳着色器,此例中仅充当简单的传递着色器,不修改任何控制点
[domain("quad")]/*面片的类型,可选用关键字有 tri、quad、isoline*/
[partitioning("integer")]/*指定曲面细分的细分模式, 有interger、fractional_even*/
[outputtopology("triangle_cw")]/*细分所创建的三角形绕序*/
[outputcontrolpoints(4)]/*hull shader执行的次数*/
[patchconstantfunc("ConstantHS")]/*指定常量外壳着色器的函数名字字段*/
[maxtessfactor(64.0f)]/*通知给驱动的使用的曲面细分因子的最大值*/
HullOut HS(InputPatch<VertexOut, 4> p,/*通过inputPatch关键字可以把patch所有的控制点都传递到hull shader之中*/
           uint i : SV_OutputControlPointID, /*系统值SV_OutputControlPointID索引那些正在被hull shader当前正在工作的输出control point*/
           uint patchId : SV_PrimitiveID /*面片ID值*/
)
{
    HullOut hout;
	
    hout.PosL = p[i].PosL;
	
    return hout;
}

struct DomainOut
{
    float4 PosH : SV_POSITION;
};

// The domain shader is called for every vertex created by the tessellator.  
// It is like the vertex shader after tessellation.
[domain("quad")]
DomainOut DS(PatchTess patchTess,
             float2 uv : SV_DomainLocation,
             const OutputPatch<HullOut, 4> quad)
{
    DomainOut dout;
	
	// Bilinear interpolation.
    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
    float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.x);
    float3 p = lerp(v1, v2, uv.y);
	
	// Displacement mapping
    p.y = 0.3f * (p.z * sin(p.x) + p.x * cos(p.z));
	
    float4 posW = mul(float4(p, 1.0f), gWorld);
    dout.PosH = mul(posW, gViewProj);
	
    return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
