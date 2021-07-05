//***************************************************************************************
// d3dApp.h by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

// 链接所需的d3d12库
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class D3DApp
{
protected:
	/* 这个构造器只负责初始化数据成员为默认值*/
	D3DApp(HINSTANCE hInstance);

	D3DApp(const D3DApp& rhs) = delete;
	D3DApp& operator=(const D3DApp& rhs) = delete;
	/* 析构函数负责释放类中所用的COM对象并刷新CommandList, 理由是CPU在等待GPU处理队列命令*/
	virtual ~D3DApp();

public:
	/* 拿取类静态指针*/
	static D3DApp* GetApp();
	/* 拿应用句柄*/
	HINSTANCE AppInst()const;
	/* 拿主窗口句柄*/
	HWND MainWnd()const;
	/* 拿后台缓存的宽高比*/
	float AspectRatio()const;
	/* 只有启用4xMSAA技术才会返回真*/
	bool Get4xMsaaState()const;
	/* 开启或禁用4XMSAA技术*/
	void Set4xMsaaState(bool value);
	/* 方法封装应用的消息循环,只要没有窗口消息到来,就会处理游戏逻辑*/
	int Run();

	/// ===== 一共是6个框架方法,共计6个虚函数,需要重写 =====

	/// 框架方法: 为渲染程序编写初始化代码,例如分配资源,初始化对象和建立3D场景
	virtual bool Initialize();

	/// 框架方法: 此方法用于实现主窗口的那个lpfnWndProc回调函数; 主要是负责重写处理消息
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
protected:
	/// 框架方法:此虚函数负责创建渲染程序所需的RTV和DSV视图堆,
	/// 在这里默认的实现是创建出来了一个具有后台缓存个数的RTV的RTV堆和具有仅1个DSV视图的DSV堆
	virtual void CreateRtvAndDsvDescriptorHeaps();

	/// 框架方法:当此前的MsgProc方法收到处理消息(或者窗口发生尺寸变化)的时候就需要调用此函数
	/// 由于投影矩阵依赖工作区尺寸,所以也在次函数里进行调整
	virtual void OnResize();

	/// 框架方法:每一帧调用,更新3D渲染程序, 比如呈现动画,移动摄像机,检查输入
	virtual void Update(const GameTimer& gt) = 0;
	/// 框架方法:每一帧绘制的时候调用,发出渲染命令真正地绘制到后台缓存
	virtual void Draw(const GameTimer& gt) = 0;

	// 便于重写鼠标输入消息的处理流程
	virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:
	/* 初始化应用程序的主窗口,与Windows编程有关*/
	bool InitMainWindow();
	/* Direct3D的初始化流程 目前仅达到第六步创建描述符堆,剩余的四步在OnResize方法内*/
	bool InitDirect3D();
	/* 创建命令队列\分配器\命令列表*/
	void CreateCommandObjects();
	/* 创建交换链*/
	void CreateSwapChain();
	/* 强制CPU等待GPU,直到GPU处理完队列所有命令*/
	void FlushCommandQueue();
	/* 拿取交换链内当前合适的那个后台缓存*/
	ID3D12Resource* CurrentBackBuffer()const;
	/* 根据已有的RtvHeap, 利用这个C3D12构造函数来 依据偏移值(实际上就是第几块后台缓存)查找后台缓存里的RTV*/
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
	/* 根据已有的DSVHeap, 拿到深度模板资源的视图句柄*/
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;
	/* 计算FPS和 单帧毫秒时长*/
	void CalculateFrameStats();

	void LogAdapters();// 枚举系统中所有显卡
	void LogAdapterOutputs(IDXGIAdapter* adapter);// 枚举指定显卡的显示器
	void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);// 枚举某个显示器对于特定格式支持的所有显示模式

protected:

	static D3DApp* mApp;// D3DApp类静态指针

	HINSTANCE mhAppInst = nullptr; // 应用程序句柄
	HWND      mhMainWnd = nullptr; // 主窗口句柄
	bool      mAppPaused = false;  // 应用程序是否暂停
	bool      mMinimized = false;  // 应用程序是否最小化
	bool      mMaximized = false;  // 应用程序是否最大化
	bool      mResizing = false;   // 大小调整栏是否受到拖拽
	bool      mFullscreenState = false;// 开否开启全屏模式

	bool      m4xMsaaState = false;    // 4X MSAA 技术启用开关
	UINT      m4xMsaaQuality = 0;      // 4X MSAA质量级别

	// 时间计数器类用于记录帧间隔 以及 游戏总时长
	GameTimer mTimer;

	Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;// IDXGIFactory4是DXGI中关键接口
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;// 逻辑设备

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	UINT64 mCurrentFence = 0;// 当前围栏序号

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;// 命令队列,GPU控制
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;// 命令分配器COM对象
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;// 命令列表COM对象,CPU控制

	static const int SwapChainBufferCount = 2;// 交换链中缓存个数
	int mCurrBackBuffer = 0;// 交换链中当前缓存序数,默认为0
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;// ID3D12DescriptorHeap对象,渲染目标视图堆
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;// 深度模板视图堆

	D3D12_VIEWPORT mScreenViewport;// 视口对象
	D3D12_RECT mScissorRect;// 裁剪矩形对象

	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;// 视图堆中邻接CBV增量大小

	// 用户应在D3DApp的子类的子类构造器里自定义这些初始值
	std::wstring mMainWndCaption = L"d3d App";
	D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;// 后台缓存格式
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;// 用以填充清除值的深度模板格式
	int mClientWidth = 800;
	int mClientHeight = 600;
};

