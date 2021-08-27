#include "FrameResource.h"

/// 构造器负责创建分配器并 构建出 PASSCB、SSAOCB、MaterialBuffer、ObjectCB这4个Uploader,各自存有指定数量的元素
FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    /* 先创建1个命令分配器*/
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);// 构建 passCount个数量的 PassCB上传堆
    SsaoCB = std::make_unique<UploadBuffer<SsaoConstants>>(device, 1, true);// 构建存有 1个数量的 SsaoCB上传堆 
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);// 构建存有 materialCount数量的 上传堆Buffer
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);// 构建存有objectCount数量的 上传堆buffer
}

FrameResource::~FrameResource()
{

}