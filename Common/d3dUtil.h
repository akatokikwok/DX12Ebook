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
    if(obj)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12Device* obj, const char* name)
{
    if(obj)
    {
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12DeviceChild* obj, const char* name)
{
    if(obj)
    {
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

    /* 将某种缓存区大小变为256B的整数倍,目的是来适配硬件按256倍规格来查看偏移量*/
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

    /*
    * 加载.cso文件字节码到程序中
    */
    static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

    /*使用d3dUtil::CreateDefaultBuffer来避免重复使用默认堆操作GPU资源 
    * 常用于创建顶点\索引缓存
    * 参数initData:数据源, byteSize:此种数据的大小 */
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
	UINT IndexCount = 0;// 索引数量
	UINT StartIndexLocation = 0;// 全局顶点/索引缓存 中 起始索引
	INT BaseVertexLocation = 0;// 全局顶点/索引缓存 中 基准地址

    // 当前子网格中所存有的 几何体包围盒
    DirectX::BoundingBox Bounds;
};

/// 当需要绘制多个几何体,就是用此结构体
struct MeshGeometry
{
	// 指定此几何体的名字,方便后续利用名字查找
	std::string Name;

    /// 由于顶点/索引可以是泛型格式,故允许用Blob来表示;待合适时机在转换为适当类型格式
    /// 注意!!!这里是后缀为CPU,它们都是系统内存中的副本
    Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU  = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // 与缓存区相关联的数据
	UINT VertexByteStride = 0;// 顶点字节偏移
	UINT VertexBufferByteSize = 0;// 顶点缓存字节大小
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;// 索引格式
	UINT IndexBufferByteSize = 0;// 索引缓存字节大小

    /// 一张表
    /// 一个MeshGeometry结构体允许存储一组顶点\索引缓存区 的多个几何体
    /// 使用下列无序map就可以定义subMeshGeometry几何体, 并允许单独地绘制出其中的子网格(即单个几何体)
    std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

    /* 利用几何体类成员数据返回一个顶点缓存视图*/
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

    /* 利用几何体类成员数据返回一个索引缓存视图*/
	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

	/* 待数据上传至GPU后的时机, 就可以置空上传资源nullptr, 以此释放这些成员数据里的内存了*/
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

struct Light
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
};

#define MaxLights 16

struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

// Simple struct to represent a material for our demos.  A production 3D engine
// would likely create a class hierarchy of Materials.
// 定义在Util.h的材质结构体
struct Material
{
	// 便于查找材质的唯一名称
	std::string Name;

	// 本材质的常量缓存区索引,默认为-1
	int MatCBIndex = -1;

	// 漫反射纹理位于SRV堆中的索引, 默认为-1
	int DiffuseSrvHeapIndex = -1;

	// Index into SRV heap for normal texture.
	int NormalSrvHeapIndex = -1;

	// "脏标记", 用以表示本材质已有变动,提示更新常数缓存
    // 每个帧资源都持有一个材质常量, 所以要对所有帧资源执行更新, 所以这里把"脏标记" 等价于 帧资源个数
	int NumFramesDirty = gNumFrameResources;

	// 用于shader着色的 "材质常量缓存数据"
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };// 漫反射率
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };// 菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
	float Roughness = .25f;// 粗糙度(和菲涅尔系数一起用于控制镜面光),越大越粗糙
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();// 材质变换矩阵
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