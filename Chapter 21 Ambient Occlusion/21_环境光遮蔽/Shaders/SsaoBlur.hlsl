//=============================================================================
// SsaoBlur.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// 由于深度缓存的精度限制,可能会产生错误的渲染效果,所以变远变模糊的SSAO(双边模糊) 让SSAO图的过渡更加平滑
// 为环境光图执行双边模糊.以像素着色器取代之前工程的计算着色器 避免从计算模式向渲染模式的转变
// texture cache一定程度缓解了不具备共享内存的缺点.环境光图采用16位纹理格式,占用空间较小,可用于存储大量纹素
//=============================================================================

/* SSAO要使用到的常量*/
cbuffer cbSsao : register(b0)
{
    float4x4 gProj; // 投影矩阵, 由于其具有z_ndc = A + B / viewZ的公式,可以被拿来做一些算法设计, 被用来参与NdcDepthToViewDepth函数内部的运算
    float4x4 gInvProj; // 投影矩阵的逆矩阵 负责把位于NDC空间的四边形的corner point变换到近裁剪面窗口上
    float4x4 gProjTex; // 投影纹理变换矩阵, 负责投影 q点 到 p所在物体 上落成1个 r点
    float4 gOffsetVectors[14]; // 8角点+6面中心点,以此得出来的14个偏移向量向量
    
    float4 gBlurWeights[3]; // 用于给模糊操作,给 SsaoBlur.hlsl用的模糊权重数组

    float2 gInvRenderTargetSize; // 用于模糊逻辑里的texOffset计算

    // 指定观察空间中的各个坐标
    float gOcclusionRadius;    // 遮蔽半径    
    float gOcclusionFadeStart; // 遮蔽衰落起始
    float gOcclusionFadeEnd;   // 遮蔽衰减结束
    float gSurfaceEpsilon;     // 用户自定义的容错范围值
};

/* SSAO模糊处理要使用到的常量*/
cbuffer cbRootConstants : register(b1)
{
    bool gHorizontalBlur;// 是否开启水平模糊
};

Texture2D gNormalMap : register(t0); // 本shader持有一张2D法线图纹理
Texture2D gDepthMap : register(t1);  // 本shader持有一张2D深度图纹理
Texture2D gInputMap : register(t2);  // inputMap负责被点采样参与计算权重模糊颜色
 
SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);

static const int gBlurRadius = 5; // 模糊半径,暂设为5

/* full screen quad的6个角点*/ 
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

/* VS主要负责输出6个角点为纹理坐标TexC, 并同时把6个角点从屏幕空间变换到NDC空间用作PosH*/
VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    vout.TexC = gTexCoords[vid]; // 根据SV_VertexID拿到任意角点并把它填充到输出顶点的 "纹理坐标"里

    // 将打在屏幕上的全屏四边形 从屏幕空间 变换到 NDC空间
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

    return vout;
}

/* 此函数负责把某个点的深度 从NDC空间重建到 观察空间*/
float NdcDepthToViewDepth(float z_ndc)
{
    /* 我们可以执行 把z坐标(深度)从NDC空间变换至View Space的逆运算,
     * 由于存在有" z_ndc = A + B / viewZ, 即化简为 z_view = B / (z_ndc - A)
     * 且又知其中gProj[2][2] == A、gProj[3][2] == B",因此可以借助这个小技巧等式来执行推算
     */
    float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
    return viewZ;
}

/* 目的是为了经保边模糊(双边模糊)对噪点进行平滑处理*/
float4 PS(VertexOut pin) : SV_Target
{
    // 模糊权重解包到数组 blurWeights里
    float blurWeights[12] =
    {
        gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
        gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
        gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
    };

    float2 texOffset; // 声明一个偏移
    if (gHorizontalBlur)  {//根据水平模糊开关决定各自逻辑
        texOffset = float2(gInvRenderTargetSize.x, 0.0f);
    } else {
        texOffset = float2(0.0f, gInvRenderTargetSize.y);
    }
    
	// 总是把中心值计算到总权重里
    float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp/*点采样*/, pin.TexC/*其实就是角点*/, 0.0);
    float totalWeight = blurWeights[gBlurRadius];// 因为有12个元素,取[5]即第6个就是中心权重
	 
    float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz; // 点采样法线贴图以获取像素的中心法线值
    float centerDepth = NdcDepthToViewDepth(
        gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r); // 采样深度纹理以获取中心值像素在NDC空间里的深度 并将深度其重建至观察空间

    // 遍历正负模糊半径
    for (float i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
		// 此前已经计入了中心权重
        if (i == 0)
            continue;

        float2 tex = pin.TexC + i * texOffset; // 计算本迭代的偏移纹理坐标

        /*效仿 相邻像素在NDC空间里的z坐标(即深度值) 并将其重建至观察空间*/
        float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz; // 用本次偏移的纹理坐标作为查找位置, 采样法线纹理以得到相邻法线
        float neighborDepth = NdcDepthToViewDepth(
            gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r// 用本次偏移的纹理坐标作为查找位置, 也采样深度图以获取相邻像素点深度并把深度重建到 观察空间
        );

		//
		// 如果中心值与相邻值相差过大(不论法线还是深度),就假设正在采集的部分是不连续的(即处于物体边缘),继而放弃对该样本后续的模糊操作
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
