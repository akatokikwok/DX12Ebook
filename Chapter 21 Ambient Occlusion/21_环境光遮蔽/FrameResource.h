#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"

/* 单帧的ObjectCB*/
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();       // 物体CB需要有自己的 世界变换矩阵
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();// 物体CB需要有自己的 object纹理变换矩阵
	UINT     MaterialIndex;										 // 物体CB需要有自己的 材质索引
	UINT     ObjPad0;
	UINT     ObjPad1;
	UINT     ObjPad2;
};

/* 单帧的PASSCB*/
struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProjTex = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ShadowTransform = MathHelper::Identity4x4();// 阴影图要用的 ShadowTransform
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };// 眼睛位置
	float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };// 阴影图要用的 RenderTargetSize
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;

	DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };// 每帧PASS的环境光, 用以间接光

	Light Lights[MaxLights];// 光源数组
};

/* 单帧的SSAOCB*/
struct SsaoConstants
{
	DirectX::XMFLOAT4X4 Proj;// 投影矩阵, 由于其具有z_ndc = A + B / Z_view的 公式,可以被拿来做一些算法设计
	DirectX::XMFLOAT4X4 InvProj;// 投影矩阵的逆矩阵,和Proj紧密联系
	DirectX::XMFLOAT4X4 ProjTex;// 投影q点用的投影纹理变换矩阵
	DirectX::XMFLOAT4   OffsetVectors[14];// 8角点+6面中心点,以此得出来的14个随机分布向量

	// SsaoBlur.hlsl用的双边模糊权重
	DirectX::XMFLOAT4 BlurWeights[3];

	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

	// 都是在View Space下才生效
	float OcclusionRadius = 0.5f;// 遮蔽半径
	float OcclusionFadeStart = 0.2f;// 遮蔽衰落起始
	float OcclusionFadeEnd = 2.0f;// 遮蔽衰减结束
	float SurfaceEpsilon = 0.05f;// 用户自定义的容错范围值
};

/* 单帧的结构化材质*/
struct MaterialData
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };// 单帧结构化材质里 持有1个漫反照率
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };		 // 单帧结构化材质里 持有1个菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
	float Roughness = 0.5f;										 // 单帧结构化材质里 持有1个粗糙度(和菲涅尔系数一起用于控制镜面光), 越大越粗糙
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();// 单帧结构化材质里 持有一个 MatTransform,用于纹理映射
	UINT DiffuseMapIndex = 0;									 // 单帧结构化材质里 持有一张 2D漫反射纹理
	UINT NormalMapIndex = 0;									 // 单帧结构化材质里 持有一张 法线纹理
	UINT MaterialPad1;
	UINT MaterialPad2;
};

/* 环境光遮蔽项目用到的顶点类型*/
struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
	DirectX::XMFLOAT3 TangentU;
};

/* 保存CPU需要的来构建帧命令列表的所有资源
 * FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount) 构造器
 */
struct FrameResource
{
public:
	
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);// 构造器
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;// 每帧都要有自己的命令分配器

	/* 我们不能更新一个cbuffer，直到GPU处理了引用它的命令。所以每一帧都需要它们自己的cbuffer*/
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;// 每帧都要有自己的PASSCB
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;// 每帧都要有自己的ObjectCB
	std::unique_ptr<UploadBuffer<SsaoConstants>> SsaoCB = nullptr;// 每帧都要有自己 SSAO效果的CB

	std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;// 每帧都要有自己的结构化材质

	UINT64 Fence = 0;// 每帧都有自己的围栏,用以检测CPU\GPU的交互与同步
};