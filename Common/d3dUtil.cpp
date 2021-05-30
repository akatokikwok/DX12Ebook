
#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
	ErrorCode(hr),
	FunctionName(functionName),
	Filename(filename),
	LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
	return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
	std::ifstream fin(filename, std::ios::binary);

	fin.seekg(0, std::ios_base::end);
	std::ifstream::pos_type size = (int)fin.tellg();
	fin.seekg(0, std::ios_base::beg);

	ComPtr<ID3DBlob> blob;
	ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

	fin.read((char*)blob->GetBufferPointer(), size);
	fin.close();

	return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,// 被拷贝到默认缓存区的真正数据,一般是顶点数组数据
	UINT64 byteSize,// 缓存区所占字节数,一般是顶点数量*sizeof(顶点结构体)
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer// 声明的上传堆
)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// 创建实际的默认缓存资源
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// 要把CPU端内存里的数据拷贝到默认缓存, 还需要创建一个处于中介位置的资源 上传堆
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	// 描述我们需要拷贝至默认缓存区的数组数据
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;// 欲复制资源的字节数
	subResourceData.SlicePitch = subResourceData.RowPitch;// 也是欲复制资源的字节数

	// 利用资源屏障及Transition方法 把舒服拷贝到默认缓存区的流程;先把默认缓存资源从普通状态切换为被拷贝状态
	// UpdateSubresources助手方法会将数据从CPU端 拷贝到 处在中介位置的上传堆里暂存
	// 再通过调用ID3D12CommandList::CopySubResourceRegion方法,把上传堆的数据再拷贝到 mBuffer里
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
	);
	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ)
	);

	/// 注意,调用上述函数之后,要确保uploadBuffer不能立即销毁,原因是列表里的复制操作可能没执行完
	/// 等到调用者完成复制,就可以释放uploadBuffer

	return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	// 若处于调试模式,则使用调试标志
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

std::wstring DxException::ToString()const
{
	// Get the string description of the error code.
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}


