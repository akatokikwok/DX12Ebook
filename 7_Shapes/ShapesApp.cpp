//***************************************************************************************
// ShapesApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************

#include <iostream>
#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "../Common/d3dUtil.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;// 一个由3个帧资源构建的向量

// 单次绘制向管线提交的有关多个绘制物数据叫作"渲染项"
// 成员有1.某几何体worldMatrix 2.帧脏标记(值为帧资源数量) 3.常数缓存序数 4.几何体模型Geo 5.图元 6.DrawIndexedInstanced方法的3个参数
struct RenderItem
{
	RenderItem() = default;
	XMFLOAT4X4 World = MathHelper::Identity4x4();// 绘制物世界矩阵

	/// 程序准备以脏标记证明渲染物的数据发生改变,暗示着需要更新常数缓存
	/// 由于FrameResource里有各种类型的 常数缓存变量, 故需要更新每一个FrameResouce
	int NumFramesDirty = gNumFrameResources;
	// "第几个缓存区",该索引指向的常数缓存,对应于 当前渲染项中的物体常数缓存
	// 与上传堆里更新常数缓存的CopyData(int elementIndex, const T& data)第一参数有关系
	UINT ObjCBIndex = -1;
	// 此渲染项参与绘制的几何体(即什么模型), !!PS,绘制一个模型可能用到多个渲染项
	MeshGeometry* Geo = nullptr;
	// 图元拓扑
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// //DrawIndexedInstanced 这个方法的一些参数
	UINT IndexCount = 0;        // DrawIndexedInstanced 这个方法的参数之一, 索引数量
	UINT StartIndexLocation = 0;// DrawIndexedInstanced 这个方法的参数之一, 起始索引
	int  BaseVertexLocation = 0;// DrawIndexedInstanced 这个方法的参数之一, 基准地址
};

/// ShapesApp应用程序,继承自D3DApp类
class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;
private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	// 一些操作,供给那些大框架用
	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();

	// 这些是:较之上一个DEMO,新增的函数功能
	void BuildFrameResources();// 填充"帧资源"数组
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	/* 习题:绘制骷髅头*/
	void BuildSkullGeometry();
private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;// 帧资源集
	FrameResource* mCurrFrameResource = nullptr;// 当前使用的帧资源指针
	int mCurrFrameResourceIndex = 0;// 当前使用的帧资源序数

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;// 根签名
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;// 所有的常数缓存视图堆(包含PassCB和ObjectCB)

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	// 使用无序表根据名称 在常数时间内查找和引用所需的资源对象(比如 每个绘制物, PSO, 纹理, shader)
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;// 输入布局

	std::vector<std::unique_ptr<RenderItem>> mAllRitems; // 存放所有的渲染项,不同的渲染项有各自不同的绘制目的,并结合在不同的PSO里
	std::vector<RenderItem*> mOpaqueRitems;              // 供PSO使用的渲染项,目前所有渲染项均为非透明

	PassConstants mMainPassCB;// PassConstants型常数缓存实例

	UINT mPassCbvOffset = 0;// "渲染过程"(PassCB)的视图起始偏移量 ;保存渲染过程(即PASSCB这种CBUFFER)的起始偏移量(因为先3n个ObjectCB,最后才是3个RenderPass)

	bool mIsWireframe = false;// 线框模式

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };// 摄像机位置
	XMFLOAT4X4 mView = MathHelper::Identity4x4();// 视图矩阵
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();// 透视矩阵

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try {
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

/// 为渲染程序编写初始化代码,例如分配资源,初始化对象和建立3D场景
bool ShapesApp::Initialize()
{
	// 先检查并执行基类的Initialize
	if (!D3DApp::Initialize())
		return false;

	// !!!!记得重置命令列表,复用内存, 为初始化命令做好准备
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	// 1.1 利用根参数(选型为描述符表),创建根签名
	BuildRootSignature();
	// 1.2 编译HLSL shader且初始化输入布局
	BuildShadersAndInputLayout();
	// 1.3 创建几何体缓存, 缓存绘制所需参数, 以及绘制物体的具体过程
	BuildShapeGeometry();

	BuildSkullGeometry();

	// 1.4 构建各种几何体的渲染项, 并存到渲染项总集
	BuildRenderItems();
	// 1.5 构建3个FrameResource, 其中passCount为1, objCount为渲染项个数
	BuildFrameResources();
	// 1.6 构建CBV用来存储(若有3个帧资源, n个渲染项), 创建出3(n+1)个CBV, 且创建出常数缓存视图堆
	BuildDescriptorHeaps();
	// 1.7 为3n+1个描述符创建出 "物体CBV" 和 "渲染过程CBV"
	BuildConstantBufferViews();
	// 1.8 创建2种管线状态
	BuildPSOs();

	// 上面代码都是记录命令,此处开始关闭上述的命令列表的记录,在队列中执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 同步强制CPU等待GPU操作,刷新队列,等待GPU处理完所有事(使用了围栏技术)
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// 检查用户是不是重新调整了窗口尺寸,更新宽高比并重新计算投影矩阵
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

/// 重载框架方法:每一帧调用,更新3D渲染程序, 比如呈现动画,移动摄像机,检查输入
/// 这里还需要额外处理CPU端第n帧的帧资源
void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	//**************CPU端处理第n帧的算法*********************

	// 1. 循环往复获取帧资源数组中的 元素frameResource指针
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;// 循环拿当前帧资源index
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();// 循环拿当前frameResource裸指针

	// 2. 监测GPU是否执行完当前帧命令,若还在执行中就强令CPU等待,直到GPU抵达围栏点
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
	// 3. 更新 当前使用帧资源的值(此处是2种常数缓存,)
	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	// 1.1 Reset分配器, 复用记录命令所用的内存
	// 注意!! 只有当GPU中所有的命令列表都执行完, 才可以重置分配器
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;// 拿取当前帧资源里的命令分配器
	ThrowIfFailed(cmdListAlloc->Reset());

	// 1.2 当利用函数ExecuteCommandList把列表都加入队列之后,就可以重置列表
	// 复用命令列表内存(需要分配器和PSO),此项目里设置了2种PSO
	if (mIsWireframe) {
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else {
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	// 1.3 设置视口和裁剪矩形
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// 2. 依据资源的使用用途 指示其状态切换,此处把资源(后台缓存)从呈现切换为渲染目标状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// 3.1 清除后台缓存(渲染目标) 和 深度缓存
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// 3.2 在管线上设置 欲渲染的目标缓存区(此处是后台缓存), 需要两个视图(RTV和DSV)的句柄(偏移查找出来)
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// 4. mCBVGHeap存成一个描述符数组 并在命令列表上设置 CBuffer视图堆
	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 4.1 命令列表设置根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// 4.2 偏移全部的CBUFFER视图堆里的 渲染过程CBV
	// 在命令列表里设置 DescriptorTable
	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;// 渲染过程CBV索引等于 mPassCBVOffset + 当前帧序数
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);// 在命令列表里设置 DescriptorTable(需要具体的cbv句柄)

	// 4.3 绘制所有已经成功加工过的渲染项, 指定要渲染的目标缓存
	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// 5. 切换资源从渲染目标切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	//6. 结束命令记录
	ThrowIfFailed(mCommandList->Close());

	// 7. 组建命令列表数组并添加到队列真正执行命令
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 8. 交换交换链里前后台呈现图像
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// 9.1 !!!更新当前frameresource里的围栏 为 基类自增后的围栏, 把命令标记到围栏点
	mCurrFrameResource->Fence = ++D3DApp::mCurrentFence;

	// 9.2 向命令队列添加一条指令专门用来设置一个新的围栏点
	// 在GPU处理完Signal()之前它不会设置额外的围栏
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

	//***********************原理科普************************
	// GPU快于CPU,则GPU进入空闲,GPU没有被充分利用
	// 我们希望CPU快于GPU,则CPU多出来的空闲时间用于处理GAMEPLAY逻辑
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0) {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0) {
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

/// 更新ObjectConstants型常数缓存,供Update()调用
void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	// 首先拿到当前帧资源里ObjectCB裸指针
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	// 遍历所有渲染项(单个渲染项含有单次绘制所有绘制物数据)
	// e是每次的渲染项
	for (auto& e : mAllRitems) {
		// 只要常量发生改变, 帧脏标记就必须对所有RenderItem执行更新
		if (e->NumFramesDirty > 0) {
			// 每次的渲染项里都存有物体的世界矩阵数据, 把它做成数据源,更新常数缓存用
			XMMATRIX world = XMLoadFloat4x4(&e->World);//
			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			// 当前帧常数缓存得到更新(使用上传堆的CopyData方法)
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// 还需要对下一个FrameResource执行更新
			e->NumFramesDirty--;// 用--是因为条件里e->NumFramesDirty > 0
		}
	}
}

/// 更新PassConstants型常数缓存,供Update()调用
void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	// 填充结构体mMainPassCB的所有成员值
	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();// 拿到当前帧资源里PassCB裸指针
	currPassCB->CopyData(0, mMainPassCB);
}


/// 创建根签名
void ShapesApp::BuildRootSignature()
{
	/// 因为此时有2种常数缓存,更新频率各异,故有2个CBV,
	/// 所以要更新根签名来获取2个描述符table, 每个物体的CBV则要针对性的依据自身渲染项进行配置
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV/*描述符表的类型*/, 1/*这张表里的描述符的数量*/, 0/*绑定到HLSL着色器的槽位序数*/);
	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	// 声明带 2 个元素的 根参数数组
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	// 结合上述根参数数组 选型并创建出 2个CBV的描述符table
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// 根签名由一系列根参数构成
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 初始化序列根签名, 此处的serializedRootSig指向1个仅有单个CBUFFER组成的描述符)
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	// 创建出最终的根签名
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

/// 编译shader且初始化输入布局
void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

/// 创建几何体缓存, 缓存绘制所需参数, 以及绘制物体的具体过程
void ShapesApp::BuildShapeGeometry()
{
	/// *********************提前构造好绘制物,并提前计算好每个模型在全局顶点/索引缓存里的 偏移量

	// 1. 先用GemometryGenerator里的算法 构造出4种模型MeshData
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);// 模型box 的meshdata
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);// 模型grid 的meshdata
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);// 模型 sphere 的meshdata
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);// 模型cylinder 的meshdata  

	// 对合并了的全局顶点缓存里每个绘制物的起始索引 进行缓存
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// 对合并了的全局索引缓存里每个绘制物的起始索引 进行缓存
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = boxIndexOffset + (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	/// ***********这些定义的 多个SubmeshGeometry结构体里包含有顶点/索引缓存内不同种类 模型 的子网格数据**********

	// 2. 填充4种子几何体的索引数, 起始索引, 基准地址

	// 用box模型MeshData的索引数量\起始索引\基准地址更新box子网格
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;
	// 用grid模型MeshData的索引数量\起始索引\基准地址更新grid子网格
	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;
	// 用sphere模型MeshData的索引数量\起始索引\基准地址更新sphere子网格
	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
	// 用cylinder模型MeshData的索引数量\起始索引\基准地址更新cylinder子网格
	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	/// *****************提取出所需要的顶点元素, 再把所有绘制物的顶点装进一个全局顶点缓存区********************

	// 3. 构建出一个 全局顶点集(数量就是这些所有绘制物点的总和)
	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);// 开辟出全局顶点内存,vertices是全局顶点集
	// 4.1 遍历4种绘制物里 Meshdata里所有的顶点,给全局顶点集里的顶点属性插值
	UINT k = 0;// K是全局的序数    
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
	}
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}
	// 4.2. 遍历4种绘制物里 Meshdata里所有的索引,给全局索引集里的索引插值
	std::vector<std::uint16_t> indices;// indices是全局索引集
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	// 4.3 附带计算出全局顶点\索引缓存字节大小
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);// 全局顶点缓存字节大小
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);// 全局索引缓存字节大小

	// 5.构建出"几何体"这种资源, 它负责管控所有子几何体, 并初始化名字"shapeGeo", 即所有的渲染项共用1个MeshGeometry
	auto geo = std::make_unique<MeshGeometry>();// geo是几何体指针,它持有很多子几何体,需要对geo成员执行填充值
	geo->Name = "shapeGeo";

	// 6.1.为geo的顶点缓存这种内存数据开辟出blob内存块, 并用此前的全局顶点集作为数据源 拷贝填充到 "父级管控者"geo里去
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	// 6.2.为geo的索引缓存这种内存数据开辟出blob内存块, 并用此前的全局索引集作为数据源 拷贝填充到 "父级管控者"geo里去
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	// 7.1 利用工具方法创建出几何体的全局顶点缓存/索引缓存 , 并设置单顶点字节偏移, 全局顶点字节大小, 索引格式, 全局索引字节大小
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
	);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
	);
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;
	// 7.2 几何体里哈希表 依据名字而对应的"子几何体" 被各自填充
	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	// 8. 转移geo几何体资源数据 到 绘制物无序表
	mGeometries[geo->Name] = std::move(geo);// 渲染程序类里的"几何体无序表" 接受 geo的资源转移
}

/// 绘制骷髅头
void ShapesApp::BuildSkullGeometry()
{
	/* 1. 读取硬盘里 模型的具体文件*/
	std::ifstream fin("Models/skull.txt");
	if (!fin)//如果读取失败则弹框警告
	{
		MessageBox(0, L"Models/skull.txt not found", 0, 0);
		std::cout << "meizhaodao!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
		return;
	}
	/* 2. 读取文件里的内容,这里是读取顶点及索引*/
	UINT vertexCount = 0;
	UINT triangleCount = 0;
	std::string ignore;
	fin >> ignore >> vertexCount;//读取vertexCount并赋值
	fin >> ignore >> triangleCount;//读取triangleCount并赋值
	fin >> ignore >> ignore >> ignore >> ignore;//整行不读
	/* 2.1 初始化模型的顶点列表, 元素个数为文件里读取出的个数*/
	std::vector<Vertex> vertices(vertexCount);// 初始化顶点列表
	/* 2.1.1 顶点列表赋值*/
	for (UINT i = 0; i < vertexCount; i++) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;//读取顶点坐标
		fin >> ignore >> ignore >> ignore;//normal不读取
		vertices[i].Color = XMFLOAT4(DirectX::Colors::PaleVioletRed);//设置顶点色
	}
	/* 文件中字符串的第四位至第六位由于都是法线,此处不读它*/
	fin >> ignore;
	fin >> ignore;
	fin >> ignore;
	/* 2.2 初始化索引列表*/
	std::vector<std::uint32_t> indices32(triangleCount * 3);
	//索引列表赋值
	for (UINT i = 0; i < triangleCount; i++) {
		fin >> indices32[i * 3 + 0] >> indices32[i * 3 + 1] >> indices32[i * 3 + 2];
	}

	fin.close();//关闭输入流

	/* 拿到 顶点\索引列表 2种缓存的大小*/
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);// 顶点缓存大小
	const UINT ibByteSize = (UINT)indices32.size() * sizeof(std::uint32_t);// 索引缓存大小 (!注意这里是uint32_t)

	/* 3.0 构建出"几何体"这种资源, 它负责管控所有子几何体, 并初始化名字"skullGeo", 即所有的渲染项共用1个MeshGeometry*/
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	/* 3.1 将顶点和索引数据复制到 几何体geo的CPU系统内存上*/
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices32.data(), ibByteSize);

	/* 3.2 将顶点和索引数据从CPU内存复制到GPU缓存上*/
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
	);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices32.data(), ibByteSize, geo->IndexBufferUploader
	);
	/* 3.3 设置被填充好数据的几何体geo的一些属性 单顶点字节偏移, 全局顶点字节大小, 索引格式, 全局索引字节大小*/
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;

	/* 3.4 声明骷髅几何体的子mesh, 此子mesh用以填充几何体的无序表<string, SubGeometry>
	* 用骷髅模型MeshData的索引数量\起始索引\基准地址更新box子网格*/
	UINT skullVertexOffset = 0;
	UINT skullIndexOffset = 0;

	SubmeshGeometry skullSubmesh;
	skullSubmesh.BaseVertexLocation = skullVertexOffset;
	skullSubmesh.StartIndexLocation = skullIndexOffset;
	skullSubmesh.IndexCount = (UINT)indices32.size();
	// 4.0 几何体里哈希表 依据名字而对应的"子几何体" 被各自填充	
	geo->DrawArgs["skull"] = skullSubmesh;
	// 4.1 转移geo几何体资源数据 到 绘制物无序表
	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));


	//
	// PSO for opaque wireframe objects.
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

/// 构建3个帧资源构成的帧资源集
void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(
			std::make_unique<FrameResource>(md3dDevice.Get(), 1/*1个passCB*/, (UINT)mAllRitems.size()/*objctCB数量*/)
		);
	}
}

/// 由于所有渲染项共用一个MeshGeometry, 此处利用DrawArgs绘制顶点/索引缓存的子区域(单个模型)
void ShapesApp::BuildRenderItems()
{
	// 1.1 构建box子几何体的渲染项并存到渲染项总集里
	auto boxRitem = std::make_unique<RenderItem>();// 构建box子几何体的渲染项
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(1, 1, 1) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));// 初始化一个矩阵填充box渲染项的世界矩阵
	boxRitem->ObjCBIndex = 0;// box渲染项设置为1号常数缓存
	boxRitem->Geo = mGeometries["shapeGeo"].get();// box渲染项里的几何体设置为 几何体无序表的shapeGeo
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;// box渲染项图元设置为三角形列表
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;// box渲染项索引数量设置为 子几何体"box"的索引数量
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;// box渲染项索引数量设置为 子几何体"box"的起始索引
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;// box渲染项索引数量设置为 子几何体"box"的基准地址
	mAllRitems.push_back(std::move(boxRitem));// 把box渲染项存到 总渲染项里


	// 1.2 构建gridx子几何体的渲染项并存到渲染项总集里    
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 1;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	/* 1.3 骷髅头的渲染项*/
	auto skullRitem = std::make_unique<RenderItem>();
	//skullRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 1.0f, 10.0f));// 设置位置和缩放
	skullRitem->ObjCBIndex = 2;//skull常量数据（world矩阵）在objConstantBuffer索引1上
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	mAllRitems.push_back(std::move(skullRitem));


	// 1.3 分别构建左边4个球体/柱体, 右边4个球体/柱体的渲染项,并加到渲染项总集
	//UINT objCBIndex = 2;// 此前有2种模型(box,grid了),故把索引数量设置为2
	UINT objCBIndex = 3;
	for (int i = 0; i < 5; ++i) {
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	// 把渲染项总集里的所有渲染项都添加到非透明渲染项集合里(演示程序里所有渲染项先默认是非透明)
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

/// 构建CBV用来存储(若有3个帧资源, n个渲染项), 创建出3(n+1)个CBV, 且创建出常数缓存视图堆
void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();// 所有的渲染项数量,此处是n个RenderItem*    
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;// 总CBV数量等于 (渲染项数量 +1 ) * 帧资源个数 即3n个objectCB+3个PassCB

	mPassCbvOffset = objCount * gNumFrameResources;// !!!!保存渲染过程(即PASSCB这种CBUFFER)的起始偏移量(因为先3n个ObjectCB,最后才是3个RenderPass)

	// 填充cbvHeapDesc// 借助cbvHeapDesc创建 常数缓存视图堆
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

/// 为3n+1个描述符创建出 "物体CBV" 和 "渲染过程CBV"
void ShapesApp::BuildConstantBufferViews()
{
	/// ********原理介绍********
	/// 描述符0到描述符n-1包含了 第0个帧资源的"物体CBV"
	/// 描述符n到描述符2n-1包含了第1个帧资源的"物体CBV"
	/// 描述符2n到描述符3n-1包含了第2个帧资源的"物体CBV"
	/// 3n, 3n+1, 3n+2存有第0个,第1个,第2个帧资源的"PASS cbv(即渲染过程CBV)"

	// 0. 预处理,让帧资源中的Objct结构体 255对齐, 并拿到所有渲染项的个数 3n+3,存成一个变量
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));//单objctCbuffer字节
	UINT objCount = (UINT)mOpaqueRitems.size();// 渲染项个数也是物体个数

	// 1. 核心想法:3个帧资源中每个帧资源中的每一个物体都需要一个对应的CBV, 所以遍历所有FrameResource
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();//拿到第几帧资源中 物体Cbuffer裸指针, 它其实是1个D3D12资源

		// 对于每个帧资源里的每个渲染项(也是每个物体)
		for (UINT i = 0; i < objCount; ++i) {
			// 偏移到第i个物体的常量缓存区
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();// 拿单帧资源里单个物体CB的 GPU地址
			cbAddress += i * objCBByteSize;// 偏移到 第i帧资源中第i个渲染项(也是第i个物体)的GPU地址

			// 偏移到该物体在堆中的 "物体CBV"
			int heapIndex = frameIndex * objCount + i;//堆序数(也是偏移个数)等于 帧序数*渲染项个数 + i
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//从mCBVHeap里拿句柄
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);// 对全局CBVHeap里的具备执行偏移, 偏移个数为之前计算好的heapIndex
			// 填充 "物体" CBVDesc
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;
			// 为每个物体创建一个CBV
			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	// 2.  3(n+1)个描述符中的最后3个描述符 依次是每个帧资源持有的 渲染过程CBV(PassCB View)
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();// 拿到第几帧资源中 有关RenderPass的Cbuffer裸指针
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();// 拿到这个D3D12资源的GPU地址

		// 偏移到在堆中的 "渲染过程CBV"
		int heapIndex = mPassCbvOffset + frameIndex;// 堆序数(也就是偏移个数) 等于帧资源序数 + PASSCB起始偏移(mPassCbvOffset已在BuildDesciptorHeaps函数里写过值了)
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		// 填充 "渲染过程" CBVDesc
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;
		// 创建出 "渲染过程" CBV
		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

/// 绘制已成功加工过的所有渲染项
void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));// 单物体CB字节大小
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();// 当前在使用的帧资源里 的 "物体CB"

	// 遍历每个外部渲染项
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];//第i个外部渲染项
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());// 设置管线顶点缓存(需要几何体内部顶点缓存视图)
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());// 设置管线索引缓存(需要几何体内部索引缓存视图)
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);// 设置图元

		// 偏移到此帧里渲染项中的CBV视图
		// 为了绘制此前帧资源 及 当前物体, 需要偏移至 视图堆中对应的CBV
		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;//CBV索引 == 帧资源序数*渲染项个数 + 第i个渲染项内部的第几号缓存
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());// 拿到全局视图堆句柄
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);// 偏移到堆中指定 CBV序数的 CBV

		// 设定此帧绘制调用所需描述符(借助偏移的CBV句柄)
		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		// 对于第i个外部渲染项, 就要绘制 一次 ,执行DrawIndexedInstanced指令
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}