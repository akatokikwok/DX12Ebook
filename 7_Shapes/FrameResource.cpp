#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount)
{
	// 创建一个命令分配器
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	// 实例化两个常数缓存指针
	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
}

FrameResource::~FrameResource()
{

}