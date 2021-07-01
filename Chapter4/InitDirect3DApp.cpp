//***************************************************************************************
// Init Direct3D.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Demonstrates the sample framework by initializing Direct3D, clearing 
// the screen, and displaying frame stats.
//
//***************************************************************************************

#include "../Common/d3dApp.h"
#include <DirectXColors.h>

using namespace DirectX;

/* 继承自D3DApp类*/
class InitDirect3DApp : public D3DApp
{
public:
	InitDirect3DApp(HINSTANCE hInstance);
	~InitDirect3DApp();

	virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
				   PSTR cmdLine, int showCmd)
{
	// 给调试版本开启运行时内存监测,监督内存泄漏
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    try
    {
        InitDirect3DApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
: D3DApp(hInstance) 
{
}

InitDirect3DApp::~InitDirect3DApp()
{
}

bool InitDirect3DApp::Initialize()
{
	// 检查父类D3DAPP有没有初始化
    if(!D3DApp::Initialize())
		return false;
		
	return true;
}

void InitDirect3DApp::OnResize()
{
	// 执行父类D3DAPP的处理消息
	D3DApp::OnResize();
}

void InitDirect3DApp::Update(const GameTimer& gt)
{

}

void InitDirect3DApp::Draw(const GameTimer& gt)
{
	// 为了重复使用"用来记录命令"的内存, 重置命令分配器
	// 只有当与GPU相关联的命令列表执行完成
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// 注意调用时机!!!当队列执行了ExecuteCommandList函数来讲某个命令列表加入队列之后,
	// 就可以重置该命令列表,来复用命令列表及其内存
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	/// ***************************以下全部都是对命令的记录***************************

	// 对资源进行状态切换, 把资源(此处是交换链中当前后台缓存)从呈现状态切换为渲染目标状态
	mCommandList->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

    // 设置视口和裁剪矩形,注意这两个也需要随着命令列表的重置而被重置
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 清除后台缓存视图 和 深度模板缓存视图
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	
    // 指定将要渲染的缓存(当前后台缓存和深度模板缓存)
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
	
    // 再次堆资源状态执行切换,把从渲染目标状态切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // 此时已经完成了命令列表里一系列的命令记录,所以关闭命令列表
	// !!!!在执行mCommandQueue->ExecuteCommandLists方法前一定要把命令列表关闭
	ThrowIfFailed(mCommandList->Close());
	/// ******************************************结束命令列表记录*********************

    // 待执行命令列表被加入队列
	// 在队列里真正执行命令
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };// 先以现有命令列表组件一个列表数组
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	
	// 双缓存情况下先呈现缓存资源, 再交换前台后台缓存
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// 强制CPU等待GPU,直到GPU完成所有命令处理
	FlushCommandQueue();
}
