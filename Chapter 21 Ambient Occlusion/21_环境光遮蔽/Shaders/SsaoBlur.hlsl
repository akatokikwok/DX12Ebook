//=============================================================================
// SsaoBlur.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// 由于深度缓存的精度限制,可能会产生错误的渲染效果,所以变远变模糊的SSAO(双边模糊) 让SSAO图的过渡更加平滑
// 为环境光图执行双边模糊.以像素着色器取代之前工程的计算着色器 避免从计算模式向渲染模式的转变
// texture cache一定程度缓解了不具备共享内存的缺点.环境光图采用16位纹理格式,占用空间较小,可用于存储大量纹素
//=============================================================================

cbuffer cbSsao : register(b0)
{
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gProjTex;
    float4 gOffsetVectors[14];

    // 用于给模糊操作
    float4 gBlurWeights[3];

    float2 gInvRenderTargetSize;

    // Coordinates given in view space.
    float gOcclusionRadius;
    float gOcclusionFadeStart;
    float gOcclusionFadeEnd;
    float gSurfaceEpsilon;

    
};

cbuffer cbRootConstants : register(b1)
{
    bool gHorizontalBlur;
};

Texture2D gNormalMap : register(t0); // 本shader持有一张2D法线图纹理
Texture2D gDepthMap : register(t1);  // 本shader持有一张2D深度图纹理
Texture2D gInputMap : register(t2);  // 
 
SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);

static const int gBlurRadius = 5;// 模糊半径,暂设为5
 
static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

/* 经过顶点着色器处理然后输出的顶点*/
struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

/* 利用构成quad的6个顶点进行绘制调用*/
VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    vout.TexC = gTexCoords[vid];

    // 将打在屏幕上的全屏四边形 从齐次裁剪空间 变换到 NDC空间
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

    return vout;
}

/* 思路介绍:
 * 由于与v同起点共方向的射线也经过点P,所以必定存在一个比例t,满足P=tv,P和V的深度也满足这个比例,所以推导出 P == (Pz / Vz) v
 * 供给像素着色器用并负责"重建观察空间位置"的函数逻辑如下
 */
float NdcDepthToViewDepth(float z_ndc)
{
    /* 我们可以执行 把z坐标从NDC空间变换至View Space的逆运算,由于存在有" z_ndc = A + B / viewZ, 且其中gProj[2][2] == A、gProj[3][2] == B",因此可以借助这个小技巧等式来执行推算*/
    float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
    return viewZ;
}

/* 目的是为了经保变模糊对噪点进行平滑处理*/
float4 PS(VertexOut pin) : SV_Target
{
    // 模糊权重解包到数组里
    float blurWeights[12] =
    {
        gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
        gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
        gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
    };

    float2 texOffset;
    if (gHorizontalBlur)//根据水平模糊开关决定各自逻辑
    {
        texOffset = float2(gInvRenderTargetSize.x, 0.0f);
    }
    else
    {
        texOffset = float2(0.0f, gInvRenderTargetSize.y);
    }

	//总是把中心值计算到总权重里
    float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0);
    float totalWeight = blurWeights[gBlurRadius];
	 
    float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz; // 采样法线贴图以获取像素的中心法线值
    float centerDepth = NdcDepthToViewDepth(
        gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r); // 获取中心值像素在NDC空间里的z坐标(即深度值) 并将其重建至观察空间

    for (float i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
		// 此前已经计入了中心权重
        if (i == 0)
            continue;

        float2 tex = pin.TexC + i * texOffset;// 计算本次迭代的偏移纹理坐标

        /*效仿 相邻像素在NDC空间里的z坐标(即深度值) 并将其重建至观察空间*/
        float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz;
        float neighborDepth = NdcDepthToViewDepth(
            gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r);

		//
		// 如果中心值与相邻值相差过大(不论法线还是深度),就执行正在采集的部分是不连续的(即处于物体边缘),即放弃对该样本后续的模糊操作
        //
        if (dot(neighborNormal, centerNormal) >= 0.8f &&
		    abs(neighborDepth - centerDepth) <= 0.2f)
        {
            float weight = blurWeights[i + gBlurRadius];

			// 累加邻近的像素颜色以执行模糊处理
            color += weight * gInputMap.SampleLevel(gsamPointClamp, tex, 0.0);
		
            totalWeight += weight;
        }
    }

	// 重新把总权重和压缩为1,来弥补被忽略而未计入统计的样本
    return color / totalWeight;
}
