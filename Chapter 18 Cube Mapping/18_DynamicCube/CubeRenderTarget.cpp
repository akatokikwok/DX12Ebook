//***************************************************************************************
// CubeRenderTarget.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "CubeRenderTarget.h"

/// (5)设置CubeMap的ViewPort和ScissorRect
/// 初始化成员变量、视口、裁剪矩形、CubeMap资源
CubeRenderTarget::CubeRenderTarget(ID3D12Device* device, 
	                       UINT width, UINT height,
                           DXGI_FORMAT format)
{
	md3dDevice = device;

	mWidth = width;
	mHeight = height;
	mFormat = format;
	//视口填充（512*512的视口渲染CubeMap）
	//viewPort.TopLeftX = 0.0f;
	//viewPort.TopLeftY = 0.0f;
	//viewPort.Width = static_cast<float>(width);
	//viewPort.Height = static_cast<float>(height);
	//viewPort.MinDepth = 0.0f;
	//viewPort.MaxDepth = 1.0f;
	mViewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
	//裁剪矩形填充（矩形外的像素都将被剔除）
	//前两个为左上点坐标，后两个为右下点坐标
	//scissorRect.left = 0;
	//scissorRect.top = 0;
	//scissorRect.right = width;
	//scissorRect.bottom = height;
	mScissorRect = { 0, 0, (int)width, (int)height };
	/*初始化CubeMap资源*/
	BuildResource();
}

ID3D12Resource*  CubeRenderTarget::Resource()
{
	return mCubeMap.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE CubeRenderTarget::Srv()
{
	return mhGpuSrv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE CubeRenderTarget::Rtv(int faceIndex)
{
	return mhCpuRtv[faceIndex];
}

D3D12_VIEWPORT CubeRenderTarget::Viewport()const
{
	return mViewport;
}

D3D12_RECT CubeRenderTarget::ScissorRect()const
{
	return mScissorRect;
}

/// （3）构建动态CubeMap资源的RTV和SRV描述符
/// 供主APP使用,此函数负责 暂存SRV和RTV描述符堆句柄的引用, 为立方体图资源创建SRV和6个面的RTV描述符
void CubeRenderTarget::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
	                                CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
	                                CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv[6])
{
	//暂存SRV和RTV描述符堆句柄的引用，传入构建描述符
	mhCpuSrv = hCpuSrv;
	mhGpuSrv = hGpuSrv;

	for(int i = 0; i < 6; ++i)
		mhCpuRtv[i] = hCpuRtv[i];

	//为立方体图资源创建SRV和6个面的RTV描述符
	BuildDescriptors();
}

void CubeRenderTarget::OnResize(UINT newWidth, UINT newHeight)
{
	if((mWidth != newWidth) || (mHeight != newHeight))
	{
		mWidth = newWidth;
		mHeight = newHeight;

		BuildResource();

		// New resource, so we need new descriptors to that resource.
		BuildDescriptors();
	}
}
 
/// （3）构建CubeMap所需描述符 (1个SRV 和 6个面每个面1个RTV)
void CubeRenderTarget::BuildDescriptors()
{
	// 先为整个CUBEMAP资源 创建出 SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;// 注意此处是TextureCUBE型
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(mCubeMap.Get(), &srvDesc, mhCpuSrv);

	// 给6个面每个面创建RTV
	for(int i = 0; i < 6; ++i)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc; 
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.Format = mFormat;
		rtvDesc.Texture2DArray.MipSlice = 0;
		rtvDesc.Texture2DArray.PlaneSlice = 0;

		// Render target to ith element.
		rtvDesc.Texture2DArray.FirstArraySlice = i;

		// Only view one element of the array.
		rtvDesc.Texture2DArray.ArraySize = 1;

		// Create RTV to ith cubemap face.
		md3dDevice->CreateRenderTargetView(mCubeMap.Get(), &rtvDesc, mhCpuRtv[i]);
	}
}

/// 动态CubeMap来实现实时反射(1): 创建一种D3D资源给Cubemap; 即具有6个元素的纹理数组（每个元素都对应一个CubeMap的面）
void CubeRenderTarget::BuildResource()
{
	// 创建资源描述结构体
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 6;// 注意：纹理数组的元素个数要写6
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;// 注意：Flag也必须是ALLOW_RENDER_TARGET
	// 创建具有6个元素的纹理数组（每个元素都对应一个CubeMap的面）
	// 上传CubeMap资源到默认堆中
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, // 不指定优化清除组
		IID_PPV_ARGS(&mCubeMap)));
}