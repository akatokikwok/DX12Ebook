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

// 持有4个纹理元素的纹理数组,每个纹理元素中都持有独特的树木纹理
Texture2DArray gTreeMapArray : register(t0); // 这里使用专属的类型,纹理数组

// 一些采样器
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
    Light gLights[MaxLights]; // 光源数组在主PASS CB里
};

// 每种材质区分的常量
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
    float2 SizeW : SIZE; // 存放有公告牌的宽和高
};

struct VertexOut
{
    float3 CenterW : POSITION; // 公告牌中心点
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

/// 顶点shader
VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// 在这节顶点shader里 直接把数据传入 几何shader
    vout.CenterW = vin.PosW;
    vout.SizeW = vin.SizeW;

    return vout;
}
 
// 该着色器功能是把公告牌中间的1个点扩展为1个四边形,并使之与y轴对齐以始终正对眼睛
// 这个语法表示设置shader定义之前的最大vertex数量; 4是几何shader单次调用所输出顶点的最大数量
// 输出的标量个数为 顶点类型结构体中标量个数 * maxvertexcount
[maxvertexcount(4)]
void GS(point VertexOut gin[1],
        uint primID : SV_PrimitiveID, // 若指定SV_PrimitiveID语义,则在输入装配阶段会为每个图元生成图元ID从0~n-1,几何着色器仅把图元ID写到输出的顶点里,以此作为一种信息传出到像素着色器阶段,而像素着色器会把图元ID用作纹理数组的索引
  inout TriangleStream<GeoOut> triStream // 输出参数必须要有inout修饰符,且必须是流类型
  // 对于线条和三角形而言,几何shader输出的对应图元必定是线条strip和三角形strip,至于线条列表和三角形列表可借助ResstartStrip实现
)
{
	//
	// 计算广告牌相对于世界的局部坐标空间，使广告牌与y轴对齐，面向眼睛.
	
	// 参见P409的图
    float3 up = float3(0.0f, 1.0f, 0.0f); // 公告牌四边形沿y轴向上单位向量
    float3 look = gEyePosW - gin[0].CenterW; // 从公告牌四边形几何中心指向眼睛的向量
    look.y = 0.0f; // y轴对齐,投影到xz平面
    look = normalize(look); // 规范化
    float3 right = cross(up, look); // 把look和up叉积得到right

	//
	// Compute triangle strip vertices (quad) in world space.计算三角形带在世界空间的4个顶点(即那个被扩展开的四边形)
	//
    float halfWidth = 0.5f * gin[0].SizeW.x;
    float halfHeight = 0.5f * gin[0].SizeW.y;
    float4 v[4];
    v[0] = float4(gin[0].CenterW + halfWidth * right - halfHeight * up, 1.0f);
    v[1] = float4(gin[0].CenterW + halfWidth * right + halfHeight * up, 1.0f);
    v[2] = float4(gin[0].CenterW - halfWidth * right - halfHeight * up, 1.0f);
    v[3] = float4(gin[0].CenterW - halfWidth * right + halfHeight * up, 1.0f);

	//
	// 将四边形的顶点从局部坐标系变换到世界空间里,并将它们以三角形带 模式输出
	//
	
    float2 texC[4] = // 有一组带4个UV
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
        gout.PrimID = primID; // 几何着色器仅把图元ID写到输出的顶点里,以此作为一种信息传出到像素着色器阶段,而像素着色器会把图元ID用作纹理数组的索引
		
        triStream.Append(gout); // 使用Append内置方法向输出流列表 添加单个顶点
    }
}

/// 像素着色器
float4 PS(GeoOut pin) : SV_Target
{
    // 纹理数组使用之前GS阶段传出来的图元ID信息,把它当做纹理数组的索引; 第3参数是纹理数组的索引;
    // 使用了纹理数组,可以让设置纹理和绘制调用都减少到只有一次
    float3 uvw = float3(pin.TexC, pin.PrimID % 3);
    // 对纹理数组执行采样 提取此像素的漫反射率 并与 材质常量里的反照率 相乘
    float4 diffuseAlbedo = gTreeMapArray.Sample(gsamAnisotropicWrap, uvw) * gDiffuseAlbedo;
	
#ifdef ALPHA_TEST
	// 忽视纹理阿尔法值小于0.1的像素,要尽早做完,以便快速退出着色器;让那些像素片段剔除掉
	clip(diffuseAlbedo.a - 0.1f);
#endif

    // 对法线插值会导致其非规范化,所以这里重新规范化一下
    pin.NormalW = normalize(pin.NormalW);

    // 从微表面1点指向眼睛
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    // 环境光照 == 漫反照率 * 杂项常量里的环境光灯源
    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    // 直接光照计算
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);
    // 点亮的最终颜色 == 环境光照 + 直接光照
    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - gFogStart) / gFogRange);
	litColor = lerp(litColor, gFogColor, fogAmount);
#endif

    // 点亮颜色的阿尔法值 从漫反照率里取
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


