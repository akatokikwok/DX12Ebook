//=============================================================================
// 把场景里各物体的观察空间法线渲染到 与screen space(即像素着色器输入的地方)等大的纹理图内,同时还需要绑定存有一块置有场景深度的普通 深度模板缓存
// !!!即理解为 把场景位于观察空间的法线绘制到1个full screen RenderTarget, 并把场景深度绘制到1个普通的depth/stencil缓存
// Ssao.hlsl 在布置好观察空间法线和场景深度后,禁用深度缓存,并在每个像素处调用SSAO的像素着色器绘制一个全屏四边形,该shader生成一个对应的环境光可及率数据
// 此过程生成的纹理图称之为 SSAP Map
// 出于性能考量,仅用深度缓存的1/2宽高来渲染SSAO图
//=============================================================================
// 这张shader主要负责SSAO的渲染过程,此时的深度缓存已经被禁用,不需要再做任何改动,详见P628图片
// p是正在处理的像素点、 而 "眼睛" 到 "p点在近裁剪面对应投射点" 的向量为v、 结合p点在深度缓存里的深度值 来重建像素点p
// q是p为球心半球面上的随机一点, r是"眼睛" 到 q点路径上的最近可视点
// r点被视作p的遮蔽点的条件: p r深度差要足够小, 且 r-p与p点法向量n夹角足够小,小于90°

/* SSAO要使用到的常量*/
cbuffer cbSsao : register(b0)
{
    float4x4 gProj;             // 投影矩阵, 由于其具有z_ndc = A + B / viewZ的公式,可以被拿来做一些算法设计, 被用来参与NdcDepthToViewDepth函数内部的运算
    float4x4 gInvProj;          // 投影矩阵的逆矩阵 负责把位于NDC空间的四边形的corner point变换到近裁剪面窗口上
    float4x4 gProjTex;          // 投影纹理变换矩阵, 负责投影 q点 到 p所在物体 上落成1个 r点
    float4   gOffsetVectors[14];// 8角点+6面中心点,以此得出来的14个偏移向量向量
    
    float4 gBlurWeights[3];     // 给 SsaoBlur.hlsl用的模糊权重数组

    float2 gInvRenderTargetSize; // 用于模糊逻辑里的texOffset计算

    // 指定观察空间中的各个坐标
    float gOcclusionRadius;   // 遮蔽半径  
    float gOcclusionFadeStart;// 遮蔽衰落起始
    float gOcclusionFadeEnd;  // 遮蔽衰减结束
    float gSurfaceEpsilon;    // 用户自定义的容错范围值
};

/* SSAO模糊处理要使用到的常量*/
cbuffer cbRootConstants : register(b1)
{
    bool gHorizontalBlur;// 是否开启水平模糊
};
 
Texture2D gNormalMap : register(t0);    // SSAO处理shader 持有一张2D法线图纹理
Texture2D gDepthMap : register(t1);     // SSAO处理shader 持有一张2D深度图纹理
Texture2D gRandomVecMap : register(t2); // SSAO处理shader 持有一张执行遮蔽检测用的多个随机向量的纹理

SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap : register(s2);
SamplerState gsamLinearWrap : register(s3);

static const int gSampleCount = 14;// 14次 8+6

/* 目的:绘制全屏四边形,因此对SSAO图中的每个像素 调用SSAO像素着色器,要借助投影矩阵的逆矩阵
 * 可以利用 "InvProj" 把位于NDC空间的四边形的6个角点变换到近裁剪面窗口上
 */
static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};
 
/* 经由VS加工过后的顶点型*/
struct VertexOut
{
    float4 PosH : SV_POSITION; // PosH表示这个QUAD上的角点,从屏幕空间被变换至NDC空间的角点,(目的是为了后续一步再变换到近裁剪面)
    float3 PosV : POSITION;    // PosV表示的是被"投影逆矩阵"变换到近裁剪面上的角点,即理解为从眼睛指向近裁剪面上角点像素的那个向量"v"
    float2 TexC : TEXCOORD0;
};

/* 利用构成quad的6个顶点进行绘制调用, 注意,只有PosV才是我们真正想要的,只有PosV会继续进入像素着色器参与计算*/
VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    vout.TexC = gTexCoords[vid];// quad6个角点对应角点的UV(即纹理坐标)

    /* 科普: 如果需要从屏幕空间转换到NDC空间,则求得ScreenPos后进行下面公式转换：
     * float4 ndcPos = (o.screenPos / o.screenPos.w) * 2 - 1;
     */
    /* 将展示在屏幕上的全屏四边形 从screen space 变换到 NDC空间*/
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);
 
    /* 将全屏quad的6个角点 再从NDC space 变换至 View Space的近裁剪面*/
    float4 ph = mul(vout.PosH, gInvProj); // 可以利用投影矩阵的逆矩阵把位于NDC空间的四边形的corner point变换到View space近裁剪面窗口上
    vout.PosV = ph.xyz / ph.w;            // 这个PosV本质上就是PosView即:从眼睛指向近裁剪面上角点像素的那个向量"v",且v和p是共线的,且P == (Pz / Vz) v

    return vout;
}

/* 此函数负责描述 点p被点r的遮蔽严重程度 */
// 得到了潜在遮蔽点r(r点是视线到随机向量q点的延长线打到q物体上的落点, 存在r = rz/qz * q), 也就能够执行遮蔽检测 来估算 p点是否被r点遮挡
// 若|pz - rz|绝对值过大 表明r点离p点太远了,不足以遮挡p点,此时认为q点与p点共面,即认为q点也不会遮挡p点
// p点处法线n 与向量 r-p的夹角测定方法是 max(n · normalize(r-p), 0),目的是为了阻止 自交
// 根据pr两点深度差计算遮蔽值; 目标点p被遮蔽点r遮挡严重程度的逻辑封装成一个函数
float OcclusionFunction(float distZ /* 入参是目标点p和遮蔽点r的深度差: |pz - rz| */)
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
	//        0   Epsilon     FadeStart      FadeEnd
	
    float occlusion = 0.0f;// 声明一个遮蔽值
    if (distZ > gSurfaceEpsilon)// 若pr两点深度差过大, 即大于用户定义的容忍阈值
    {
        float fadeLength = gOcclusionFadeEnd - gOcclusionFadeStart;
		
		// 随着深度差由gOcclusionFadeStart愈发趋近于gOcclusionFadeEnd,那么根据函数图像可以得出遮蔽值会线性衰减,由1衰减到0
        occlusion = saturate((gOcclusionFadeEnd - distZ) / fadeLength);
    }
	
    return occlusion;
}

/* 思路介绍: 已知"从眼睛到p落在近裁剪面上的像素点的向量 vout.PosV, 又知p深度和v深度"
 * 由于与v同起点共方向的射线也经过点P,所以必定存在一个比例t,满足P=tv,P和V的深度也满足这个比例,所以推导出 P == (Pz / Vz) v
 * 供给像素着色器用并负责"重建点p位于观察空间位置(从Zndc重建回观察空间)"的函数逻辑如下
 */
/* 此函数负责把某个点的深度 从NDC空间重建到 观察空间*/
float NdcDepthToViewDepth(float z_ndc/*深度图里某像素位于NDC空间的深度*/)
{
	/* 我们可以执行 把z坐标(深度)从NDC空间变换至View Space的逆运算,
     * 由于存在有" z_ndc = A + B / viewZ, 即化简为 z_view = B / (z_ndc - A)
     * 且又知其中gProj[2][2] == A、gProj[3][2] == B",因此可以借助这个小技巧等式来执行推算
     */
    float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
    return viewZ;
}
 
float4 PS(VertexOut pin) : SV_Target
{
	// p -- 我们要计算的遮蔽目标点 p
	// n -- 点p处的法向量 n
	// q -- 随机偏离于点p的一点 q (p为球心的半球内的随机点q)
	// r -- 有概率遮挡住p的某个点 r
    
    // 以quad里6个角点的UV为依据, 采样法线贴图以获取像素的法线
    float3 n = normalize(gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz);

    /* 先拿取此像素位于 ViewSpace里的法线 以及 深度坐标*/
    float pz = gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r;                // 以quad里6个角点的UV为依据,先从深度图里获取该像素在NDC空间里的z坐标(即深度值): Zndc,即Pz(位于NDC空间)
    pz = NdcDepthToViewDepth(pz);													 // 计算出p点在观察空间里的深度

	/* 由于上一步算出了Pz, 且又知道与v同起点共方向的射线也经过点P,所以必定存在一个比例t,满足P=tv,P和V的深度也满足这个比例,所以推导出 P == (Pz / Vz) v
	/* 这一步负责 计算出 p点在观察空间的3D位置*/
    float3 p = (pz / pin.PosV.z) * pin.PosV; // pz是p点在观察空间的深度, pin.PosV.z是近裁剪面上那个角点的深度, pin.PosV是观察点(眼睛)指向近裁剪面上的角点构成的向量"v"
	
    /* 这一步负责 采样随即向量图 gRandomVecMap并从[0,1]映射至[-1, 1] 以获取随机向量*/
	// 从随机向量图里变换并拿到随机向量 并从 [0,1] 映射到--> [-1, +1]来模拟反射效果; 以此得到专供那14个偏移向量
    float3 randVecN = 2.0f * gRandomVecMap.SampleLevel(gsamLinearWrap, 4.0f * pin.TexC, 0.0f).rgb - 1.0f;

    float occlusionSum = 0.0f;// 初始化遮蔽累加值为0
    
	// 在以p为中心的半球内,根据法线n对p周围的随机点执行采样
	// 像素着色器里执行一次随机向量构建的纹理图并使用这个纹理图来对14个均匀分布的向量执行反射,其最终结果就是获得14个均匀分布的随机向量
    for (int i = 0; i < gSampleCount; ++i)// 采样14次,计算14次
    {
        /* 这一步负责 计算出一组均匀分布的 "随机偏移向量"*/
		// 已知采样出的随机向量,且又知 偏移向量具备固定均匀分布的特性
        // 如果尝试把偏移向量关于一个随机向量执行反射,那么得到的结果必定也是一组均匀分布的随机偏移向量
        float3 offset = reflect(gOffsetVectors[i].xyz/*单个偏移向量*/, randVecN /*采样gRandomVecMap得到的随机向量*/);
	
		// 如果此 "偏移向量" 恰好不幸位于(p,n)所定义的平面的后侧,则不是我们想要的,就翻转它
        float flip = sign(dot(offset, n));// 判断flip是正数还是负数
		
        ///==========================================================
        /// 核心思想,就是以"眼睛视角" 为每个点q生成投影纹理坐标, 并据此对深度缓存采样以获取q在NDC空间中的深度 rz,接着再把q点变换至观察空间,
        /// 借着变换后的q来求取 眼睛到p点方向v上 距离眼睛最近的深度值 rz
        ///==========================================================
        
		/* 这一步负责 计算q点的 3D位置(位于观察空间)*/
        float3 q = p + flip * gOcclusionRadius * offset;
		
		// 投影点q 并生成相应的投影纹理坐标 r,即此处的变量 projQ
        float4 projQ = mul(float4(q, 1.0f), gProjTex /*负责投影 q点 到 p所在物体 上落成1个 r点*/);
        projQ /= projQ.w;

        /* 这一步负责 以点q的"投影纹理坐标"为依据 采样深度图 gDepthMap获得r点深度(NDC), 并把r点深度重建到观察空间*/
		// 沿着从眼睛到q的观察方向,查找离眼睛最近的深度值(它未必是q的深度值,因为q只是接近于p的任意一点,q处有概率是空气而非实体)
		// 为此,就必须查看那个点在深度图中的深度值
        float rz = gDepthMap.SampleLevel(gsamDepthMap, projQ.xy, 0.0f).r;// 结合点q的投影纹理坐标, 采样深度图取得符合条件的点r深度,点r此时还处在NDC空间
        rz = NdcDepthToViewDepth(rz);// 深度值Rz从NDC空间变换到观察空间

		// 效仿此前的p的处理过程,重建位于观察空间的位置坐标点 r=(rx, ry, rz).
		// 已知,点r处在眼睛到点q的光径上,因此也就存在某个比例t满足 r == t * q(float3型)
		// r.z = t*q.z ==> t = r.z / q.z
        // 已知r深度、q深度、q在观察空间的3D位置
        float3 r = (rz / q.z) * q;// 利用用 q点 和 rz 算出r的位于观察空间的 3d位置
		
		//
		// 测试 "潜在的遮蔽点r" 是否遮挡着像素点p
		// 点积dot( n, normalize(r-p) ) 负责度量 遮蔽点r距离平面(p,n)前侧的距离,
		// 越趋近于此平面前侧,就给设定一个更大的遮蔽权重,(也有额外的好处,减缓倾斜面(p,n)上的某点r的自阴影诱发出来的错误遮蔽值)
		// 遮蔽权重依赖于 遮蔽点r和其目标点p的距离,距离过大则视作点r完全不会遮蔽点p
        float distZ = p.z - r.z;// p点和r点的深度差
        float dp = max(dot(n, normalize(r - p)), 0.0f); // "潜在的遮蔽点r" 距离 平面(p,n)这个平面前侧的 距离dp; 若越趋于此平面前侧,就应该给它设定更大的遮蔽权重

        float occlusion = dp * OcclusionFunction(distZ);// 利用深度差计算出新的遮蔽值

        occlusionSum += occlusion;// 叠加遮蔽值
    }
	
    occlusionSum /= gSampleCount;// 每个样点的遮蔽数据累加后,还需要除以14次采样次数来 计算平均遮蔽值
	
    float access = 1.0f - occlusionSum;// 可及率

	// 锐化增加SSAO map的对比度,让SSAO图更加容易被识别
    return saturate(pow(access, 10.0f));
}
