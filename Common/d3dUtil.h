//***************************************************************************************
// d3dUtil.h by Frank Luna (C) 2015 All Rights Reserved.
//
// General helper code.
//***************************************************************************************

#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"
#include "DDSTextureLoader.h"
#include "MathHelper.h"

extern const int gNumFrameResources;

inline void d3dSetDebugName(IDXGIObject* obj, const char* name)
{
	if (obj)     {
		obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
	}
}
inline void d3dSetDebugName(ID3D12Device* obj, const char* name)
{
	if (obj)     {
		obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
	}
}
inline void d3dSetDebugName(ID3D12DeviceChild* obj, const char* name)
{
	if (obj)     {
		obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
	}
}

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

/*
#if defined(_DEBUG)
	#ifndef Assert
	#define Assert(x, description)                                  \
	{                                                               \
		static bool ignoreAssert = false;                           \
		if(!ignoreAssert && !(x))                                   \
		{                                                           \
			Debug::AssertResult result = Debug::ShowAssertDialog(   \
			(L#x), description, AnsiToWString(__FILE__), __LINE__); \
		if(result == Debug::AssertIgnore)                           \
		{                                                           \
			ignoreAssert = true;                                    \
		}                                                           \
					else if(result == Debug::AssertBreak)           \
		{                                                           \
			__debugbreak();                                         \
		}                                                           \
		}                                                           \
	}
	#endif
#else
	#ifndef Assert
	#define Assert(x, description)
	#endif
#endif
	*/

class d3dUtil
{
public:

	static bool IsKeyDown(int vkeyCode);

	static std::string ToString(HRESULT hr);

	/* 将某种缓存区大小变为256B的整数倍,目的是来适配硬件按256倍规格来查看偏移量 */
	static UINT CalcConstantBufferByteSize(UINT byteSize)
	{
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}

	/* 加载.cso文件字节码到程序中 */
	static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

	/* 使用d3dUtil::CreateDefaultBuffer来避免重复使用默认堆操作GPU资源 (为了利用Upload堆来初始化DEFAULT堆中的数据)
	 * 常用于创建顶点\索引缓存
	 * 参数initData:数据源, byteSize:此种数据的大小
	 */
	static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

	/* 此辅助方法在运行时编译shader为字节码
	* .hlsl文件路径
	* 默认设为空指针
	* 着色器入口点函数名
	* 着色器类型及其版本
	*/
	static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);
};

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

	std::wstring ToString()const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};

/// 子几何体 由父级MeshGeometry持有
struct SubmeshGeometry
{
	UINT IndexCount = 0;// 单个子几何体的index count
	UINT StartIndexLocation = 0;// 全局顶点/索引缓存 中 起始index
	INT BaseVertexLocation = 0;// 全局顶点/索引缓存 中 基准地址
	
	DirectX::BoundingBox Bounds;// 当前子网格中所存有的 几何体包围盒
};

/* 当多个几何体共享全局VB和IB时候,使用此几何辅助结构体*/
struct MeshGeometry
{
	// 指定这个父级几何体的名字,方便后续利用名字查找
	std::string Name;

	/// 由于顶点/索引可以是泛型格式,故允许用Blob来表示;待合适时机在转换为适当类型格式
	/// 注意!!!这里是后缀为CPU,它们都是系统内存中的副本
	Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	/// 这些都是供GPU访问的顶点和索引缓存数据
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	/// 处于中介位置的Uploader
	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	// 与缓存区相关联的一些常用杂项数据
	UINT VertexByteStride = 0;						// 单顶点字节大小(偏移)
	UINT VertexBufferByteSize = 0;					// vertex buffer总大小
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT; // 索引格式,默认为DXGI_FORMAT_R16_UINT
	UINT IndexBufferByteSize = 0;					// index  buffer总大小

	/// 一张表
	/// 一个MeshGeometry结构体允许存储一批次的子几何体(也就是一批次的顶点集和索引集)
	/// 使用下列无序map就可以定义subMeshGeometry几何体, 并允许单独地绘制出其中的子网格(即单个几何体)
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

	/* 根据GPU维护的顶点数据, 构建出一个 vertex buffer view, 方便其设置到管线内*/
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

	/* 根据GPU维护的索引数据, 构建出一个 index buffer view, 方便其设置到管线内*/
	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

	/* 待数据上传至GPU后的时机, 就可以置空扮演中介角色的uploader, 以此释放这些成员数据里的内存了*/
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

struct Light
{
	DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };  // 光强, 光源的颜色
	float FalloffStart = 1.0f;                          // 仅供点光\聚光灯使用
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// 仅供平行光\聚光灯使用
	float FalloffEnd = 10.0f;                           // 仅供点光\聚光灯使用
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // 仅供点光\聚光灯使用
	float SpotPower = 64.0f;                            // 仅供聚光灯使用
};

#define MaxLights 16

// 定义在util.h内的材质常量缓冲区结构体
struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// 供纹理贴图使用的材质Transform
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

// 定义在Util.h的材质结构体
struct Material
{
	// 便于查找材质的唯一名称
	std::string Name;

	// 本材质的第几号缓存区, 可被外部数据源以CopyData()填充更新,默认为-1
	int MatCBIndex = -1;

	// 漫反射纹理位于SRV堆中的索引, 默认为-1
	int DiffuseSrvHeapIndex = -1;

	// 法线纹理位于SRV堆中的索引,默认为-1
	int NormalSrvHeapIndex = -1;

	// "脏标记", 用以表示本材质已有变动,提示更新常数缓存,默认为3(帧资源个数);
	// 每个帧资源都持有一个材质常量, 所以要对所有帧资源执行更新, 所以这里把"脏标记" 等价于 帧资源个数
	int NumFramesDirty = gNumFrameResources;// 材质里的帧标记,当材质发生变化,就更改此帧标记, 默认为3(即帧资源个数)

	// 用于shader着色的 "材质常量缓存数据"
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };// 漫反射率
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };// 菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
	float Roughness = .25f;// 粗糙度(和菲涅尔系数一起用于控制镜面光),越大越粗糙
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();// Material的 变换矩阵
};

struct Texture
{
	// Unique material name for lookup.
	std::string Name;

	std::wstring Filename;

	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif