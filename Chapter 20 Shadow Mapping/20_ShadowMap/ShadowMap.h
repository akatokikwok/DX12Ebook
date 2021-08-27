//***************************************************************************************
// ShadowMap.h by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"

/* 新建个类ShadowMap，用来创建深度缓冲区、深度图所用描述符、视口和裁剪矩形。阴影图实际是张深度图*/
/* ShadowMap(ID3D12Device* device, UINT width, UINT height); 构造器指定了视口和裁剪矩形 并 创建出深度图资源*/
class ShadowMap
{
public:
	ShadowMap(ID3D12Device* device,
		UINT width, UINT height);
		
	ShadowMap(const ShadowMap& rhs)=delete;
	ShadowMap& operator=(const ShadowMap& rhs)=delete;
	~ShadowMap()=default;

    UINT Width()const;
    UINT Height()const;
	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv()const;

	D3D12_VIEWPORT Viewport()const;
	D3D12_RECT ScissorRect()const;

	/// 暂存外部引用并给阴影图这种外部资源创建SRV、DSV
	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv);

	void OnResize(UINT newWidth, UINT newHeight);

private:
	void BuildDescriptors();
	/* 阴影数据准备阶段1.0, 构建1个深度缓冲区用来存放阴影图*/
	void BuildResource();

private:

	ID3D12Device* md3dDevice = nullptr;

	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	UINT mWidth = 0;
	UINT mHeight = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_R24G8_TYPELESS;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuDsv;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;// 阴影图实际是张深度图,也是一种D3D资源
};

 