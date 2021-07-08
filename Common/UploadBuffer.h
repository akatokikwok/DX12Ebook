#pragma once

#include "d3dUtil.h"

/* 
* UploadBuffer类是负责上传缓存资源的构造与析构,处理资源映射,更新缓存区特定资源的封装类
* 可以用于各种类型的上传缓存区
* UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
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
        if(isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

        // 先创建出上传缓存资源,使用上传堆来匹配CPU端操作(因为让CPU来处理常量缓存)
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer((mElementByteSize) * elementCount),//单缓存区大小 * 多少个缓存区
			D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)));

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
        if(mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr);
        // 置空欲更新数据
        mMappedData = nullptr;
    }

    /* 此方法拿上传资源mUploadBuffer (比如常数缓存)的裸指针*/
    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();
    }

    /* 将真正的内存数据,复制到 映射出的欲更新数据中, 以此达成从CPU端拷贝内存到常量缓存
    * 参数1: 第几个缓存区
    * 参数2: 真正的内存数据,数据来源 泛型T型实例, 而T为特定的常量缓存结构体类型 */
    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex*mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;// 一种上传缓存资源(一般用于常量缓存)
    BYTE* mMappedData = nullptr;// 映射出来的欲更新资源的指针

    UINT mElementByteSize = 0;// 缓存区结构体大小;若是常量缓存,则需要注意将其变为256整数倍
    bool mIsConstantBuffer = false;
};