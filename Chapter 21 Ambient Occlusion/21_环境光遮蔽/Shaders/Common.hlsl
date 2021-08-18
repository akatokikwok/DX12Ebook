//***************************************************************************************
// Common这张shader用以存放一些通用的数据和使用数据
//***************************************************************************************

// 光源的一些参数默认设置
/* 索引[0, NUM_DIR_LIGHTS]表示平行光源 
 * 索引[NUM_DIR_LIGHTS, NUM_DIR_LIGHTS + NUM_POINT_LIGHTS]表示点光源
 * 索引[ NUM_DIR_LIGHTS + NUM_POINT_LIGHTS,  NUM_DIR_LIGHTS + NUM_POINT_LIGHTS+NUM_SPOT_LIGHTS]表示聚光灯源
 */
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3 // 有3个平行光
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// 引用有关光照的头文件
#include "LightingUtil.hlsl"

// 结构化材质--对应帧资源里的struct MaterialData
struct MaterialData
{
    float4 DiffuseAlbedo;   // 单帧结构化材质里 持有1个漫反照率
    float3 FresnelR0;       // 单帧结构化材质里 持有1个菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
    float Roughness;        // 单帧结构化材质里 持有1个粗糙度(和菲涅尔系数一起用于控制镜面光), 越大越粗糙
    float4x4 MatTransform;  // 单帧结构化材质里 持有一个 MatTransform,用于纹理映射
    uint DiffuseMapIndex;   // 单帧结构化材质里 持有一张 2D漫反射纹理
    uint NormalMapIndex;    // 单帧结构化材质里 持有一张 法线纹理
    uint MatPad1;
    uint MatPad2;
};

TextureCube gCubeMap : register(t0, space0); // t0, space0, Cubemap纹理
Texture2D gShadowMap : register(t1);         // t1, space0, 一张阴影图
Texture2D gSsaoMap   : register(t2);         // t2, space0, 一张SSAO图

// 一个纹理数组，它只支持在shader模型5.1+。不像Texture2DArray，纹理
// 这个数组可以是不同的大小和格式，使它比纹理数组更灵活
Texture2D gTextureMaps[10] : register(t3);   // t3, space0, 2d Texture(10张构成数组)

// t0, space1, 结构化材质,存于space1,和CubeMap公用一个SRV寄存器，但是不同Space
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1); 

// 一系列的采样器
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

// 每帧变化的 物体常量
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;        // 每个物体都有自身的世界矩阵
    float4x4 gTexTransform; // 每个物体都有自己的纹理变换矩阵
    uint gMaterialIndex;    // 每个物体都有自己的材质索引
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

// 在每次材质变化的 PASS
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;       // 用以变换出 位于齐次裁剪空间的顶点-PosH
    float4x4 gInvViewProj;
    float4x4 gViewProjTex;    // 用以变换出 生成SSAO图里的投影纹理坐标--SsaoPosH
    float4x4 gShadowTransform;// 阴影图要用的 ShadowTransform
    float3 gEyePosW;          // 眼睛位置
    float cbPerObjectPad1;
    float2 gRenderTargetSize; // 阴影图要用的 RenderTargetSize
    float2 gInvRenderTargetSize; // 阴影图要用的 InvRenderTargetSize
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight; // 每帧PASS的环境光, 用以间接光

    // 光源数组
    Light gLights[MaxLights];
};

//---------------------------------------------------------------------------------------
// 新建NormalSampleToWorldSpace函数，将法线从切空间变换到世界空间
// 我们要做光照计算， 必须要将法线空间和灯光、摄像机等处于同一空间下。 我们选择世界空间，所以要将切线空间的法线变换到世界空间下，
// 因此要使用世界空间下的切空间坐标基来构建Tangent2World矩阵，那么前提是需要获得切空间下的法线、 切线、副切线， 而副切线可以通过法线和切线叉积计算得到，
// 所以最关键是要得到顶点切线和法线， 法线先前已经获得，所以要从创建模型时得到顶点切线数据。
//---------------------------------------------------------------------------------------
float3 NormalSampleToWorldSpace(float3 normalMapSample/*被采样法线图*/, float3 unitNormalW /*位于世界空间的顶点法线*/, float3 tangentW /*位于世界空间的顶点切线*/)
{
	// 将法线贴图每个像素的法线值从[0, 1]映射到[-1 ,1],得到法线值
    // DX12中使用Sample函数来采样法线图，要注意的是，此函数已经除以255，将法线从[0, 255]转换成了[0, 1]之间，所以欲从法线图得到法线值只需乘2减1即可
    float3 normalT = 2.0f * normalMapSample - 1.0f;// 位于切线空间的法线值

	// 规范正交基
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);
    // 构造Tangant to World矩阵
    float3x3 TBN = float3x3(T, B, N);

	// 将法线从切线空间转至世界空间
    float3 bumpedNormalW = mul(normalT, TBN);
    // 拿取到一个位于世界空间的法线
    return bumpedNormalW;
}

//---------------------------------------------------------------------------------------
/// 为阴影图构建PCF,返回阴影因子
//---------------------------------------------------------------------------------------
//#define SMAP_SIZE = (2048.0f)
//#define SMAP_DX = (1.0f / SMAP_SIZE)
float CalcShadowFactor(float4 shadowPosH /*接受外部1个顶点*/)
{
    // 将处于齐次裁剪空间未执行齐次除法的顶点变换到NDC空间（如果是正交投影，则W=1）
    shadowPosH.xyz /= shadowPosH.w;

    // 获取 处于NDC空间中的深度值
    float depth = shadowPosH.z;
    
    /* 读取阴影图--gShadowMap的宽高及mip级数*/
    uint width, height, numMips;
    gShadowMap.GetDimensions(0, width, height, numMips);

    // 纹素尺寸
    float dx = 1.0f / (float) width;
    
    /* 单次PCF预存值*/
    float percentLit = 0.0f;
    /* 使用9核, 3排3列*/
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
    };
    
    /* PCF（类似均值模糊算法）执行9次4-tap PCF*/
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        // 每个核都执行tap4 PCF计算, DX12可以调用SampleCmpLevelZero函数来执行PCF，并最大程度的优化采样过程。
		// 做PCF的采样需要使用“比较采样器”，这使硬件能够执行阴影图的比较测试，且需要在过滤采样结果之前完成
        percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowPosH.xy + offsets[i], depth).r;
        
        // 单独测试一下PCF，看下软硬阴影的区别。将PCF计算中累加的offsets数组改成4号元素，即中间的0号，这样相当于没有做PCF,会发现阴影边缘有明显的锯齿
		// percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow,shadowPosH.xy + offsets[4], depth).r;
    }
    
    /* 将9次PCF取均值*/
    return percentLit / 9.0f;
}

