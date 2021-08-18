//***************************************************************************************
// d3dApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "d3dApp.h"
#include <WindowsX.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
	return mApp;
}

D3DApp::D3DApp(HINSTANCE hInstance)
	: mhAppInst(hInstance)
{
	// Only one D3DApp can be constructed.
	assert(mApp == nullptr);
	mApp = this;
}

D3DApp::~D3DApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

HINSTANCE D3DApp::AppInst()const
{
	return mhAppInst;
}

HWND D3DApp::MainWnd()const
{
	return mhMainWnd;
}

float D3DApp::AspectRatio()const
{
	return static_cast<float>(mClientWidth) / mClientHeight;
}

bool D3DApp::Get4xMsaaState()const
{
	return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
	if (m4xMsaaState != value) {
		m4xMsaaState = value;

		// Recreate the swapchain and buffers with new multisample settings.
		CreateSwapChain();
		OnResize();
	}
}

int D3DApp::Run()
{
	MSG msg = { 0 };

	mTimer.Reset();

	while (msg.message != WM_QUIT) {
		// 使用PeekMessage持续捕获消息
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// 否则就应该正常执行 游戏逻辑
		else {
			mTimer.Tick();

			if (!mAppPaused) {
				CalculateFrameStats();
				Update(mTimer);
				Draw(mTimer);
			} else {
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}

bool D3DApp::Initialize()
{
	if (!InitMainWindow())
		return false;

	if (!InitDirect3D())
		return false;

	// Do the initial resize code.
	OnResize();

	return true;
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));// 创建渲染目标视图堆


	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));// 创建深度模板视图堆
}

void D3DApp::OnResize()
{
	assert(md3dDevice);
	assert(mSwapChain);
	assert(mDirectCmdListAlloc);

	// Flush before changing any resources.
	FlushCommandQueue();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();
	mDepthStencilBuffer.Reset();

	// Resize the swap chain.
	ThrowIfFailed(mSwapChain->ResizeBuffers(
		SwapChainBufferCount,
		mClientWidth, mClientHeight,
		mBackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrBackBuffer = 0;

	/* =====7. 为交换链里每个后台缓存创建渲染目标视图句柄 并 偏移到下一个句柄*/
	// CD3D12构造函数利用给定的偏移值可以查找到当前backbuffer的RTV
	// 使用CD3D12构造一个描述符句柄,供给后续创造RTV的时机用
	// (原则上允许可以尝试创建另一个纹理,再为它创建RTV,然后绑定到流水线的OM阶段)
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());//是一个视图句柄
	// 遍历查找交换链里每一个缓冲
	for (UINT i = 0; i < SwapChainBufferCount; i++) {
		// 拿到交换链内部第i个缓冲
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		// 为这i号缓冲创建RTV
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		// 以RTV增量偏移到视图堆里的下1个视图句柄
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	/* =====8. 创建深度/模板缓存资源及其视图句柄*/
	/// 填充深度模板的 "资源结构体"; 供给后续创建深度模板资源用CreateCommittedResource
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;// 资源维度
	depthStencilDesc.Alignment = 0;									// 
	depthStencilDesc.Width = mClientWidth;							// 纹理宽度
	depthStencilDesc.Height = mClientHeight;						// 纹理高度
	depthStencilDesc.DepthOrArraySize = 1;							// 纹理深度
	depthStencilDesc.MipLevels = 1;									// mipmap层级数量,对于深度模板缓存仅允许有一个mipmap层级
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;			// 用以指定纹素的格式
	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;		// 多重采样的质量级别以及对每个像素的采样次数
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;			// 用以指定纹理的布局
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;// 与committedResource相关的标志,在此指定为深度模板
	// 接上文,填充清除值结构体;供给后续创建深度模板资源用CreateCommittedResource
	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	/// 使用CreateCommittedResource函数创建GPU资源(这里是为了深度模板缓冲,深度模板也是一种2D纹理)
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		// 注意P111页的四种Heap_TYPE_PROPERTIES属性,标定了堆供CPU/GPU的访问权限
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),// 注意,由于是深度模板视图,所以不需要CPU读,所以放在默认堆,而非上传堆
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,// 此刻深度模板资源的管线状态设为COMMON
		&optClear,// 清除值
		IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf()))// 返回创建出来的深度模板COM指针
	);
	/// 填充专门的"深度模板描述符" 结构体; 并利用深度模板缓存描述符 为深度模板缓存资源的第0层mip层创建描述符
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	/* 创建出深度模板视图句柄(类似于创建渲染目标视图),他需要借助 深度模板资源*/
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());
	/// 利用资源屏障把深度模板缓冲从 初始COMMON状态 切换为 读写状态
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	);

	// Execute the resize commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	FlushCommandQueue();

	/* =====9. 设置视口*/
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mClientWidth);
	mScreenViewport.Height = static_cast<float>(mClientHeight);
	// PS!!注意利用minDepth和maxDepth转化归一化的深度值就可以实现某些特效, 设置这两个值为0, 表明位于此视口3D场景比其他3D更加靠前
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;
	/* =====10.设置裁剪矩形*/
	mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// 处理外部消息或者状态
	switch (msg) {

		// WM_ACTIVATE 消息会被发送 只要当渲染程序被激活或者进入非激活时
		// We pause the game when the window is deactivated and unpause it when it becomes active.  
		case WM_ACTIVATE:
			if (LOWORD(wParam) == WA_INACTIVE) {// 当程序切换为不活动时候
				mAppPaused = true;// 命中程序暂停
				mTimer.Stop();// 暂停计数器
			} else// 当程序切换为活动
			{
				mAppPaused = false;
				mTimer.Start();// 启用计数器
			}
			return 0;

			// WM_SIZE is sent 当显示窗口发生大小变化
			/// WM_SIZE核心目的是为了让后台缓存和深度缓存与工作区矩形保持一致,防止拉伸
		case WM_SIZE:
			// Save the new client area dimensions.
			mClientWidth = LOWORD(lParam);
			mClientHeight = HIWORD(lParam);
			if (md3dDevice) {
				if (wParam == SIZE_MINIMIZED) {
					mAppPaused = true;
					mMinimized = true;
					mMaximized = false;
				} else if (wParam == SIZE_MAXIMIZED) {
					mAppPaused = false;
					mMinimized = false;
					mMaximized = true;
					OnResize();
				} else if (wParam == SIZE_RESTORED) {

					// Restoring from minimized state?
					if (mMinimized) {
						mAppPaused = false;
						mMinimized = false;
						OnResize();
					}

					// Restoring from maximized state?
					else if (mMaximized) {
						mAppPaused = false;
						mMaximized = false;
						OnResize();
					} else if (mResizing) {
						// If user is dragging the resize bars, we do not resize 
						// the buffers here because as the user continuously 
						// drags the resize bars, a stream of WM_SIZE messages are
						// sent to the window, and it would be pointless (and slow)
						// to resize for each WM_SIZE message received from dragging
						// the resize bars.  So instead, we reset after the user is 
						// done resizing the window and releases the resize bars, which 
						// sends a WM_EXITSIZEMOVE message.
					} else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
					{
						OnResize();
					}
				}
			}
			return 0;

			// WM_EXITSIZEMOVE is sent 当用户抓取resize bar的时候
		case WM_ENTERSIZEMOVE:
			mAppPaused = true;// 暂停程序
			mResizing = true;// 启用重设尺寸
			mTimer.Stop();// 停用计数器
			return 0;

			// WM_EXITSIZEMOVE is sent 当用户松开释放resize bar的时候
			// 此处根据更改尺寸后的窗口大小 重设相关对象(比如缓存\视图之类的)
		case WM_EXITSIZEMOVE:
			mAppPaused = false;
			mResizing = false;
			mTimer.Start();
			OnResize();// 松开resize bar之后要调用,重设一些对象
			return 0;

			// WM_DESTROY is sent 当窗口销毁就发送销毁消息
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

			// The WM_MENUCHAR message is sent 当某种菜单被激活同时又满足非加速键位\助记键
		case WM_MENUCHAR:
			// Don't beep when we alt-enter.
			return MAKELRESULT(0, MNC_CLOSE);

			// 这个消息会被补货 当窗口尺寸变的过小
		case WM_GETMINMAXINFO:
			((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
			((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
			return 0;

		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
			OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
			OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		case WM_MOUSEMOVE:
			OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		case WM_KEYUP:
			if (wParam == VK_ESCAPE) {
				PostQuitMessage(0);
			} else if ((int)wParam == VK_F2)
				Set4xMsaaState(!m4xMsaaState);

			return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow()
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = mhAppInst;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc)) {
		MessageBox(0, L"注册窗口类失败;;;;;;;RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, mClientWidth, mClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;
	/* 创建出一个窗口 */
	mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(),
		WS_OVERLAPPEDWINDOW, /*CW_USEDEFAULT, CW_USEDEFAULT,*/1000, 350, width, height, 0, 0, mhAppInst, 0);
	if (!mhMainWnd) {
		MessageBox(0, L"窗口创建失败;;;;CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(mhMainWnd, SW_SHOW);/* 呈现窗口*/
	UpdateWindow(mhMainWnd);/* 更新窗口*/

	return true;
}

bool D3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) //第0步: 启用D3D12的调试层
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	/* ====1. 初始化D3D的第一步, 创建D3D12设备(即显卡)并记录结果*/
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // 归类到默认显卡
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&md3dDevice));

	// =====如果d3d设备创建失败,会回退到WRAP====
	if (FAILED(hardwareResult)) {
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));// 先dxgifactory枚举出WARP
		// 再创建出设备,归类到WRAP
		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&md3dDevice)));
	}
	/* =====2. 创建出1个围栏对象mFence 并获取各描述符大小(渲染目标\深度模板\常数)GetDescriptorHandleIncrementSize*/
	ThrowIfFailed(md3dDevice->CreateFence(
		0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&mFence)
	));
	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	/* =====3. 检测对4XMSAA质量级别的支持  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS结构体*/
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;// 多重采样质量级别info,此处作为输入参数
	msQualityLevels.Format = mBackBufferFormat;// 后台缓存格式
	msQualityLevels.SampleCount = 4;// 采样数量
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;// MSAA支持的flag
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(/* 查询对应的质量级别,此函数填写图像质量级别作为输出*/
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)
	));
	// 4X MSAA质量输出出来
	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");// 大部分硬件肯定支持4X MSAA

#ifdef _DEBUG
	LogAdapters();
#endif
	/* =====4. 依次创建命令队列\命令分配器\命令列表,随后结束记录命令*/
	CreateCommandObjects();
	/* =====5. 描述并创建交换链*/
	CreateSwapChain();
	/* =====6. 创建描述符堆*/
	CreateRtvAndDsvDescriptorHeaps();

	return true;
}

void D3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};// 队列
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	/* 创建出队列,IID_PPV_ARGS宏拿取ID3D12CommandQueue接口的COM ID并强转为void**型*/
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));
	/* 创建出命令分配器,命令存储在分配器里*/
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));
	/* 而创建命令列表,需要1个与之关联的分配器*/
	ThrowIfFailed(md3dDevice->CreateCommandList(
		0,/*单GPU系统设置为0*/
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(), // 与列表关联的分配器,类型必须匹配,此处均为DIRECT
		nullptr,                   // 列表的流水线初始状态
		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	// 创建完成后首先必须关闭命令列表,这是因为第一次使用列表时候需要对其重置,而一旦要重置则必须在重置前关闭
	mCommandList->Close();
}

void D3DApp::CreateSwapChain()
{
	// 释放此前创建的交换链,以方便重建交换链
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mClientWidth;// 后台缓存宽
	sd.BufferDesc.Height = mClientHeight;// 后台缓存高
	sd.BufferDesc.RefreshRate.Numerator = 60;// 后台缓存
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;// 交换链里所使用的缓存数量
	sd.OutputWindow = mhMainWnd;// 渲染窗口句柄
	sd.Windowed = true;// 指定以窗口模式显式窗口
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;// 设定为程序切换为全屏时,窗口将选择最恰当的匹配分辨率模式

	// 创建交换链,但是交换链需要借助命令队列对其进行刷新
	ThrowIfFailed(mdxgiFactory->CreateSwapChain(
		mCommandQueue.Get(),
		&sd,
		mSwapChain.GetAddressOf()));
}

void D3DApp::FlushCommandQueue()
{
	// 每次调用刷新队列的时候,就需要注意增加围栏值,为了将命令命中到此新围栏点
	mCurrentFence++;

	// 向队列中添加一条用来设置新围栏点的命令
	// 由于此命令由GPU负责处理,故GPU处理完队列Signal()之前的所有命令前, GPU不会再设置新的围栏点
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	// 强制CPU等待GPU, 持续监测围栏进行值小于当前围栏点
	if (mFence->GetCompletedValue() < mCurrentFence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);// 声明一个事件句柄

		// 当GPU命中此围栏点时候(即执行到Signal()函数修改了围栏值), 在这个围栏点上设定事件并激发
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		// 无限等待GPU命中围栏,激发事件
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* D3DApp::CurrentBackBuffer()const
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView()const
{
	/* 利用这个C3D12构造函数来 依据偏移值(实际上就是第几块后台缓存)查找后台缓存里的RTV*/
	// ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart来拿取视图堆的第一个成员视图的句柄
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),// 拿渲染目标视图堆里的第一个视图句柄
		mCurrBackBuffer,// 偏移至后台缓存的视图句柄 的索引
		mRtvDescriptorSize// 渲染目标视图字节大小
	);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView()const
{
	// ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart来拿取视图堆的第一个成员视图的句柄
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalculateFrameStats()
{
	// 这些代码计算了FPS,也计算帧渲染时间
	// 这些计算出来的数据会被附加到窗口标题里

	static int frameCnt = 0;
	static float timeElapsed = 0.0f;// 每帧所花费秒数

	frameCnt++;

	// 以一秒钟为标准计算FPS和帧渲染时间
	if ((mTimer.TotalTime() - timeElapsed) >= 1.0f) {
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

		wstring fpsStr = to_wstring(fps);
		wstring mspfStr = to_wstring(mspf);

		wstring windowText = mMainWndCaption +
			L"    每秒平均帧数: " + fpsStr +
			L"   每帧平均渲染时长: " + mspfStr + L"ms";

		SetWindowText(mhMainWnd, windowText.c_str());

		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void D3DApp::LogAdapters()
{
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;// IDXGIAdapter是适配器接口指针
	std::vector<IDXGIAdapter*> adapterList;// 已查过的显卡集
	while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {// 只要能持续查找到显卡
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);// 从显卡里取出信息存成一个desc

		// 把desc拼成一个字符串并输出
		std::wstring text = L"***Adapter: ";
		text += desc.Description;
		text += L"\n";
		OutputDebugString(text.c_str());
		// 显卡记录到已查显卡集里
		adapterList.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapterList.size(); ++i) {
		LogAdapterOutputs(adapterList[i]);
		ReleaseCom(adapterList[i]);
	}
}

void D3DApp::LogAdapterOutputs(IDXGIAdapter* adapter)
{
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugString(text.c_str());

		LogOutputDisplayModes(output, mBackBufferFormat);

		ReleaseCom(output);

		++i;
	}
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
	UINT count = 0;
	UINT flags = 0;

	// Call with nullptr to get list count.
	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (auto& x : modeList) {
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + std::to_wstring(x.Width) + L" " +
			L"Height = " + std::to_wstring(x.Height) + L" " +
			L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
			L"\n";

		::OutputDebugString(text.c_str());
	}
}