#pragma once

#include "d3dUtil.h"

/*
* UploadBuffer类是负责上传缓存资源的构造与析构,处理资源映射,更新缓存区特定资源的封装类
* 可以用于各种类型的上传缓存区
* UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
* 类构造器负责 创建出一个UploadBuffer并 使用其存储多少个元素的 特定buffer(一般用于常数缓存)
*/
template<typename T>
class UploadBuffer
{
public:
	/* 类构造器负责 创建出一个UploadBuffer并 使用其存储多少个元素的 特定buffer(一般用于常数缓存)
	* 1.256化泛型T实例的字节大小
	* 2.创建出上传堆资源来匹配CPU端
	* 3.用Map映射出上传堆资源里欲更新的数据*/
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);// 缓存区结构体大小;若是常量缓存,则需要注意将其变为256整数倍
		// 由于硬件(实则是D3D12_BUFFER_CONSTANT_BUFFER_VIEW_DESC)只能按m*256B的偏移量这种规格查看常量数据
		if (isConstantBuffer)
			mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

		// 创建出由CPU负责加工的 上传堆资源;
		// 先创建出"一种上传堆类型的缓存资源",使用上传堆"D3D12_HEAP_TYPE_UPLOAD"来匹配CPU端操作(因为让CPU来处理常量缓存)
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer((mElementByteSize)*elementCount),// 某个缓存大小 * 多少个缓存
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)));

		// 把这个'上传堆资源'映射出其内部 真正的欲更新的资源指针: mMappedData
		// 使用Map方法映射出 上传缓存资源(此处大概率是常量缓存)里的欲更新资源的指针
		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

		// 此后代码逻辑中,只要还会修改当前资源,就不需要Unmap资源,但是,若是有资源被GPU使用,就不可以写入资源
	}

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	/* 析构器负责
	* 析构的时候上传堆资源取消映射并置空欲更新数据*/
	~UploadBuffer()
	{
		// 析构的时候,就顺带取消映射上传资源
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);
		// 置空欲更新数据
		mMappedData = nullptr;
	}

	/* 拿取上传堆型缓存 的裸指针*/
	ID3D12Resource* Resource()const
	{
		return mUploadBuffer.Get();
	}

	/* 把 外部数据等宽拷贝至 第几号缓存里
	* 将真正的内存数据,复制到 映射出的欲更新数据中, 以此达成从CPU端拷贝内存到常量缓存
	* 参数1: 第几个缓存区
	* 参数2: 真正的内存数据,数据来源 泛型T型实例, 而T为特定的常量缓存结构体类型 */
	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;// 一种上传缓存资源(一般用于上传堆)

	BYTE* mMappedData = nullptr;// 从某个上传堆型ComPtr资源里 映射出来的欲更新资源的'数据指针'

	UINT mElementByteSize = 0;// 某种缓存区结构体大小;若恰好是Cubffer型缓存,则需要注意将其变为256整数倍
	bool mIsConstantBuffer = false;
};