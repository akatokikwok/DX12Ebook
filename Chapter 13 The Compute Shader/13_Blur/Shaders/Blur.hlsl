//=============================================================================
// Performs a separable Guassian blur with a blur radius up to 5 pixels.
//=============================================================================

// 常数缓存: 模糊权值; Compute shader能访问的常量缓存数据
cbuffer cbSettings : register(b0)
{
	//::::: 不能把根常量映射到位于 常数缓存里的数组元素,所以这里选择把元素都一一列出	
	
	int gBlurRadius;// 模糊半径

	// 最多支持11个模糊权值
	float w0;
	float w1;
	float w2;
	float w3;
	float w4;
	float w5;
	float w6;
	float w7;
	float w8;
	float w9;
	float w10;
};

static const int gMaxBlurRadius = 5;// 声明最大的模糊半径值

Texture2D gInput            : register(t0);// 有1张纹理;计算着色器的数据源纹理
RWTexture2D<float4> gOutput : register(u0);// 计算着色器的输出;输出资源要与无序访问视图UAV关联

#define N 256
#define CacheSize (N + 2 * gMaxBlurRadius)// N+2R理论: 分配出能够容纳N + 2R个元素的共享内存,并且在N个像素中有2R个线程要各获取2个元素
groupshared float4 gCache[CacheSize];   // 声明单个线程组内的"共享内存",共享内存含有n+2r个元素

/*======================================================
*计算模糊半径最多为5个像素的可分离高斯模糊;如下,切分为横向模糊和纵向模糊
*=======================================================
*/

/// 横向模糊计算着色器 HorzBlurCS
[numthreads(N, 1, 1)]
void HorzBlurCS(int3 groupThreadID	  : SV_GroupThreadID,  // 组内线程ID 
				int3 dispatchThreadID : SV_DispatchThreadID// 调度线程ID
)
{
	// 模糊权值数组; 11个模糊权值放在数组中便于索引
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//
	// 通过填写本地线程存储区(shared memory)来减少带宽的负载
	//若要对N个像素执行模糊处理,依据模糊半径R,我们需要加载N+2R个像素
	
	// 此线程组运行着N个线程,为了获取额外的2R个像素,需要有2R个线程再额外多采集1个像素数据
	if(groupThreadID.x < gBlurRadius)// 组内线程ID横向检查,与cbuffer里的模糊半径作比较
	{
		// 对图像边界左侧存在越界采样的情况进行保护措施:clamp操作
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];// 让每个线程都去采集纹理,并把采集结果存到共享内存里
	}
	if(groupThreadID.x >= N - gBlurRadius)
	{
		// 对图像边界右侧存在越界采样的情况进行保护措施:clamp操作
		int x = min(dispatchThreadID.x + gBlurRadius, gInput.Length.x-1);
		gCache[groupThreadID.x + 2*gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
	}

	// 针对图像边界处的越界采用执行钳位处理
	gCache[groupThreadID.x+gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy-1)];

	// 利用系统API强制命令组内其他线程各自完成各自的任务;目的是避免其他线程访问到还没有初始化的共享内存元素从而造成"不安全"
	GroupMemoryBarrierWithGroupSync();// 1个同步命令
	
	//
	// 现在模糊处理每个像素
	//

	float4 blurColor = float4(0, 0, 0, 0);// 设定一个模糊颜色
	
	for(int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.x + gBlurRadius + i;// 暂存组内线程ID+当前模糊半径+i的计数
		
		blurColor += weights[i+gBlurRadius]*gCache[k];// 更新模糊颜色为 权重数组*共享内存里
	}
	
	gOutput[dispatchThreadID.xy] = blurColor;// 最终输出的纹理更新为这个模糊颜色
}

[numthreads(1, N, 1)]
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID,
				int3 dispatchThreadID : SV_DispatchThreadID)
{
	// Put in an array for each indexing.
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//
	
	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
	if(groupThreadID.y < gBlurRadius)
	{
		// 对于图像上侧边界越界采样执行钳位处理
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
	}
	if(groupThreadID.y >= N-gBlurRadius)
	{
		// 对于图像下侧边界越界采样执行钳位处理
		int y = min(dispatchThreadID.y + gBlurRadius, gInput.Length.y-1);
		gCache[groupThreadID.y+2*gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
	}
	
	// // 对于图像边界越界采样执行钳位处理
	gCache[groupThreadID.y+gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy-1)];


	// Wait for all threads to finish.
	GroupMemoryBarrierWithGroupSync();
	
	//
	// Now blur each pixel.
	//

	float4 blurColor = float4(0, 0, 0, 0);
	
	for(int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.y + gBlurRadius + i;
		
		blurColor += weights[i+gBlurRadius]*gCache[k];
	}
	
	gOutput[dispatchThreadID.xy] = blurColor;
}