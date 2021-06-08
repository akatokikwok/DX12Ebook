#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

// 常数缓存:ObjectConstants
// 目前每个绘制物只需要一个世界矩阵,对于静态物体更新频率只需要设置1次足够
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

// 常数缓存:PassConstants, 它与着色器里的cbPass一一对应
// 思路是按更新频率划分,每次渲染过程中,仅需把本次cbPass更新一次
struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
};

// 帧资源中的顶点型,供给给Shapes用
struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

// 存有CPU为构建每一帧命令列表所需要的帧资源(如分配器, 多类型的常数缓存, 自带一个围栏点)
// 其中的数据将依据程序而异, 这取决于实际绘制所需要的资源
// FrameResource(ID3D12Device* device, UINT passCount(即PassConstants型缓存区数量), UINT objectCount(ObjectCB缓存区数量));
struct FrameResource
{
public:
	
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	// 在GPU处理完与分配器相关的命令之前,不允许重置分配器
	// 每一帧都保留自己的分配器
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	// GPU执行完和此常数缓存相关的命令之前,不允许更新常数缓存
	// 每一帧都要保留自己的常数缓存
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

	// 通过围栏将命令标记到此围栏点,允许我们监测到GPU是否仍然在使用这些帧资源
	UINT64 Fence = 0;
};