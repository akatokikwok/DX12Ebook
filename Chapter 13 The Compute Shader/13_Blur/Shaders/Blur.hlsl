//=============================================================================
// Performs a separable Guassian blur with a blur radius up to 5 pixels.
//=============================================================================

// ��������: ģ��Ȩֵ; Compute shader�ܷ��ʵĳ�����������
cbuffer cbSettings : register(b0)
{
	//::::: ���ܰѸ�����ӳ�䵽λ�� ���������������Ԫ��,��������ѡ���Ԫ�ض�һһ�г�	
	
	int gBlurRadius;// ģ���뾶

	// ���֧��11��ģ��Ȩֵ
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

static const int gMaxBlurRadius = 5;// ��������ģ���뾶ֵ

Texture2D gInput            : register(t0);// ��1������;������ɫ��������Դ����
RWTexture2D<float4> gOutput : register(u0);// ������ɫ�������;�����ԴҪ�����������ͼUAV����

#define N 256
#define CacheSize (N + 2 * gMaxBlurRadius)// N+2R����: ������ܹ�����N + 2R��Ԫ�صĹ����ڴ�,������N����������2R���߳�Ҫ����ȡ2��Ԫ��
groupshared float4 gCache[CacheSize];   // ���������߳����ڵ�"�����ڴ�",�����ڴ溬��n+2r��Ԫ��

/*======================================================
*����ģ���뾶���Ϊ5�����صĿɷ����˹ģ��;����,�з�Ϊ����ģ��������ģ��
*=======================================================
*/

/// ����ģ��������ɫ�� HorzBlurCS
[numthreads(N, 1, 1)]
void HorzBlurCS(int3 groupThreadID	  : SV_GroupThreadID,  // �����߳�ID 
				int3 dispatchThreadID : SV_DispatchThreadID// �����߳�ID
)
{
	// ģ��Ȩֵ����; 11��ģ��Ȩֵ���������б�������
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//
	// ͨ����д�����̴߳洢��(shared memory)�����ٴ���ĸ���
	//��Ҫ��N������ִ��ģ������,����ģ���뾶R,������Ҫ����N+2R������
	
	// ���߳���������N���߳�,Ϊ�˻�ȡ�����2R������,��Ҫ��2R���߳��ٶ����ɼ�1����������
	if(groupThreadID.x < gBlurRadius)// �����߳�ID������,��cbuffer���ģ���뾶���Ƚ�
	{
		// ��ͼ��߽�������Խ�������������б�����ʩ:clamp����
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];// ��ÿ���̶߳�ȥ�ɼ�����,���Ѳɼ�����浽�����ڴ���
	}
	if(groupThreadID.x >= N - gBlurRadius)
	{
		// ��ͼ��߽��Ҳ����Խ�������������б�����ʩ:clamp����
		int x = min(dispatchThreadID.x + gBlurRadius, gInput.Length.x-1);
		gCache[groupThreadID.x + 2*gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
	}

	// ���ͼ��߽紦��Խ�����ִ��ǯλ����
	gCache[groupThreadID.x+gBlurRadius] = gInput[min(dispatchThreadID.xy, gInput.Length.xy-1)];

	// ����ϵͳAPIǿ���������������̸߳�����ɸ��Ե�����;Ŀ���Ǳ��������̷߳��ʵ���û�г�ʼ���Ĺ����ڴ�Ԫ�شӶ����"����ȫ"
	GroupMemoryBarrierWithGroupSync();// 1��ͬ������
	
	//
	// ����ģ������ÿ������
	//

	float4 blurColor = float4(0, 0, 0, 0);// �趨һ��ģ����ɫ
	
	for(int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.x + gBlurRadius + i;// �ݴ������߳�ID+��ǰģ���뾶+i�ļ���
		
		blurColor += weights[i+gBlurRadius]*gCache[k];// ����ģ����ɫΪ Ȩ������*�����ڴ���
	}
	
	gOutput[dispatchThreadID.xy] = blurColor;// ����������������Ϊ���ģ����ɫ
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
		// ����ͼ���ϲ�߽�Խ�����ִ��ǯλ����
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
	}
	if(groupThreadID.y >= N-gBlurRadius)
	{
		// ����ͼ���²�߽�Խ�����ִ��ǯλ����
		int y = min(dispatchThreadID.y + gBlurRadius, gInput.Length.y-1);
		gCache[groupThreadID.y+2*gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
	}
	
	// // ����ͼ��߽�Խ�����ִ��ǯλ����
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