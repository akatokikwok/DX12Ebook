//=============================================================================
// Ssao.hlsl 在布置好观察空间法线和场景深度后,禁用深度缓存,并在每个像素处调用SSAO的像素着色器绘制一个全屏四边形,该shader生成一个对应的环境光可及率数据
// 此过程生成的纹理图称之为 SSAP Map
// 出于性能考量,仅用深度缓存的1/2宽高来渲染SSAO图
//=============================================================================

cbuffer cbSsao : register(b0)
{
    float4x4 gProj; // 投影矩阵, 由于其具有z_ndc = A + B / viewZ的特效,可以被拿来做一些算法设计
    float4x4 gInvProj;
    float4x4 gProjTex; // 投影q点用的投影纹理变换矩阵
    float4 gOffsetVectors[14];// 8角点+6面中心点,以此得出来的14个随机分布向量

    // For SsaoBlur.hlsl
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
 
// Nonnumeric values cannot be added to a cbuffer.
Texture2D gNormalMap : register(t0);    // 本shader持有一张2D法线图纹理
Texture2D gDepthMap : register(t1);		// 本shader持有一张2D深度图纹理
Texture2D gRandomVecMap : register(t2); // 执行遮蔽检测用的多个随机向量的纹理

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);

static const int gSampleCount = 14;

// 利用构成quad的6个顶点进行绘制调用
// 可以利用投影矩阵的逆矩阵把位于NDC空间的四边形的corner point变换到近裁剪面窗口上
static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};
 
struct VertexOut
{
    float4 PosH : SV_POSITION;// PosH表示原已处在齐次裁剪空间的点
    float3 PosV : POSITION;// PosV表示的是被投影逆矩阵变换到近裁剪面上的点
    float2 TexC : TEXCOORD0;
};

/* 利用构成quad的6个顶点进行绘制调用*/
VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    vout.TexC = gTexCoords[vid];// quad上对应顶点的TexC

    // 将打在屏幕上的全屏四边形 从齐次裁剪空间 变换到 NDC空间
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);
 
    // 将四边形的各角点变换至View Space的近裁剪面
    float4 ph = mul(vout.PosH, gInvProj); // 可以利用投影矩阵的逆矩阵把位于NDC空间的四边形的corner point变换到近裁剪面窗口上
    vout.PosV = ph.xyz / ph.w;

    return vout;
}

// 根据深度差计算遮蔽值; 目标点p被遮蔽点r遮挡严重程度的逻辑封装成一个函数
float OcclusionFunction(float distZ)//入参是目标点p和遮蔽点r的深度差
{
	//
	// 如果q深度处在p深度之后(即超出半球范围),则表明遮蔽点q无法遮挡点p
	// 如果q太靠近p,则表明q也不能遮挡p,原因是目前仅承认 当且仅当q位于点p之前并根据用户自定义的Epsilon值才能确定点q对点p的遮蔽程度
	// 通过下列函数来确定遮蔽值
	// 
	//
	//       1.0     -------------\
	//               |           |  \
	//               |           |    \
	//               |           |      \ 
	//               |           |        \
	//               |           |          \
	//               |           |            \
	//  ------|------|-----------|-------------|---------|--> zv
	//        0     Eps          z0            z1        
	//
	
    float occlusion = 0.0f;// 遮蔽值
    if (distZ > gSurfaceEpsilon)// 目标点p和遮蔽点q的深度差过大,大于用户自定义的阈值的话
    {
        float fadeLength = gOcclusionFadeEnd - gOcclusionFadeStart;
		
		// 随着深度差由gOcclusionFadeStart愈发趋近于gOcclusionFadeEnd,那么根据函数图像可以得出遮蔽值会线性衰减,由1衰减到0
        occlusion = saturate((gOcclusionFadeEnd - distZ) / fadeLength);
    }
	
    return occlusion;
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
 
float4 PS(VertexOut pin) : SV_Target
{
	// p -- 我们要计算的遮蔽目标点 p
	// n -- 点p处的法向量 n
	// q -- 随机偏离于点p的一点 q
	// r -- 有概率遮挡住p的某个点 r

	
	/* 先拿取此像素位于 ViewSpace里的法线 以及 深度坐标*/
    float3 n = normalize(gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz);// 采样法线贴图以获取像素的法线值
    float pz = gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r;				 // 先从深度图里获取该像素在NDC空间里的z坐标(即深度值)
    pz = NdcDepthToViewDepth(pz);													 // 再把深度值pz 重建到此像素点在 ViewSpace里的深度

	/* 由于与v同起点共方向的射线也经过点P,所以必定存在一个比例t,满足P=tv,P和V的深度也满足这个比例,所以推导出 P == (Pz / Vz) v
	// 此处的意思是 借助"此前已设计好的深度值pz" 重建此像素点在 ViewSpace的位置 */
    float3 p = (pz / pin.PosV.z) * pin.PosV;
	
	// 从 [0,1] 映射到--> [-1, +1].拿到随机向量
    float3 randVec = 2.0f * gRandomVecMap.SampleLevel(gsamLinearWrap, 4.0f * pin.TexC, 0.0f).rgb - 1.0f;

    float occlusionSum = 0.0f;
	
	// 在以p为中心的半球内,根据法线n对p周围的点执行采样
	// 像素着色器里执行一次随机向量构建的纹理图并使用这个纹理图来对14个均匀分布的向量执行反射,其最终结果就是获得14个均匀分布的随机向量
    for (int i = 0; i < gSampleCount; ++i)// 采样14次,计算14次
    {
		// 偏移向量都是固定切均匀分布
		// 如果尝试把它们关于一个随机向量进行反射,得到的也肯定是一组均匀分布的随机偏移向量
        float3 offset = reflect(gOffsetVectors[i].xyz, randVec);
	
		// 如果此偏移向量恰好不幸位于(p,n)所定义的平面,则不是我们想要的,就翻转它
        float flip = sign(dot(offset, n));
		
		// 在遮蔽半径内采集最靠近点p的那个点q
        float3 q = p + flip * gOcclusionRadius * offset;
		
		// 投影点q 并生成相应的投影纹理坐标 
        float4 projQ = mul(float4(q, 1.0f), gProjTex);
        projQ /= projQ.w;

		// 沿着从观察点到q的光线方向,查找离观察点最近的深度值(它未必是q的深度值,因为q只是接近于p的任意一点,q处有概率是空气而非实体)
		// 为此,就需要查看那个点在深度图中的深度值
        float rz = gDepthMap.SampleLevel(gsamDepthMap, projQ.xy, 0.0f).r;// 结合点q的投影纹理坐标, 采样深度图取得符合条件的点r深度,点r此时还处在NDC空间
        rz = NdcDepthToViewDepth(rz);// 深度值Rz从NDC空间变换到观察空间

		// 效仿此前的p的处理过程,重建位于观察空间的位置坐标点 r=(rx, ry, rz).
		// 已知,点r处在眼睛到点q的光径上,因此也就存在某个比例t满足 r == t * q(float3型)
		// r.z = t*q.z ==> t = r.z / q.z

        float3 r = (rz / q.z) * q;
		
		//
		// 测试点r是否遮挡着点p
		// 点积dot( n, normalize(r-p) )表示是遮蔽点r距离平面(p,n)前侧的距离,
		// 越趋近于此平面前侧,就给设定一个更大的遮蔽权重,(也有额外的好处,减缓倾斜面(p,n)上的某点r的自阴影诱发出来的错误遮蔽值)
		// 遮蔽权重依赖于 遮蔽点r和其目标点p的距离,距离过大则视作点r完全不会遮蔽点p
        float distZ = p.z - r.z;// p点和r点的深度差
        float dp = max(dot(n, normalize(r - p)), 0.0f); // 遮蔽点r距离平面(p,n)的距离

        float occlusion = dp * OcclusionFunction(distZ);// 利用深度差计算出新的遮蔽值

        occlusionSum += occlusion;// 叠加遮蔽值
    }
	
    occlusionSum /= gSampleCount;// 除以14次计算平均遮蔽值
	
    float access = 1.0f - occlusionSum;// 可及率

	// 锐化增加SSAO map的对比度,让SSAO图更加容易被识别
    return saturate(pow(access, 10.0f));
}
