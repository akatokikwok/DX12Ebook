//***************************************************************************************
// LandAndWavesApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	// 第几个缓存区
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

class LandAndWavesApp : public D3DApp
{
public:
	LandAndWavesApp(HINSTANCE hInstance);
	LandAndWavesApp(const LandAndWavesApp& rhs) = delete;
	LandAndWavesApp& operator=(const LandAndWavesApp& rhs) = delete;
	~LandAndWavesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometryBuffers();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	XMFLOAT3 GetHillsNormal(float x, float z)const;
	/* 山坡演示程序里使用的 计算高度函数f(x, z)*/
	float GetHillsHeight(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	RenderItem* mWavesRitem = nullptr;// 波浪专属渲染项

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;// 存放全局所有的渲染项,不同的渲染项有各自不同的绘制目的,并结合在不同的PSO里

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	PassConstants mMainPassCB;// 主Pass结构体, 作为数据源填充 与当前帧资源关联的 PassCB用

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

	float mSunTheta = 1.25f * XM_PI;
	float mSunPhi = XM_PIDIV4;

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
		LandAndWavesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

LandAndWavesApp::~LandAndWavesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool LandAndWavesApp::Initialize()
{
	// 先检查并执行基类的Initialize
	if (!D3DApp::Initialize())
		return false;
	// 记得重置命令列表,复用内存, 为初始化命令做好准备
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);// 额外的1步,需要在初始化时候就构建一个波浪

	BuildRootSignature();		 // 利用根参数(选型为描述符表),创建根签名
	BuildShadersAndInputLayout();// 编译HLSL shader且初始化输入布局
	BuildLandGeometry();		 // 创建几何体缓存, 缓存绘制所需参数, 以及绘制物体的具体过程(这里是山峰),技术原理详见函数
	BuildWavesGeometryBuffers(); // 同上,建立波浪的几何体
	BuildRenderItems();			 // 构建各种几何体的渲染项, 并存到渲染项总集
	//BuildRenderItems();		 //
	BuildFrameResources();		 // 有了前面的渲染项数据,构建3个FrameResource, 其中passCount为1, objCount为渲染项个数
	BuildPSOs();				 //  创建2种管线状态

	// 关闭上述的命令列表的记录,在队列中执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 同步强制CPU等待GPU操作,刷新队列,等待GPU处理完所有事(使用了围栏技术)
	FlushCommandQueue();

	return true;
}

void LandAndWavesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void LandAndWavesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);// 每帧实时监测 按下的键位,如果按下1,则更新影响绘制为线框模式的mIsWireFrame字段
	UpdateCamera(gt);   // 每帧实时监测 更新字段 Float4x4 mView

	//**************CPU端处理第n帧的算法*********************

	// 循环往复获取帧资源数组中的 元素frameResource指针
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources; // 更新当前使用的FrameResource序数
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();		  // 当前用的帧资源指针

	// 监测GPU是否执行完当前帧命令, 若还在执行中就强令CPU等待, 直到GPU抵达围栏点
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	/// 每个FrameResource都有uploader,用以为场景里的每个RenderItem存储RenderPass中的常量以及Object中的常量,WaveVb动态顶点缓存
	/// 1个渲染项对应1个物体!!!!; 3个帧资源,n个渲染项,则
	/// 公式是:若有3个帧资源和n个渲染项, 则对应有3n个ObjectCB和3个PassCB

	// 更新 3种关联 当前使用帧资源的Uploader资源 (此处是2种常数缓存,和1种动态顶点缓存 )
	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void LandAndWavesApp::Draw(const GameTimer& gt)
{
	// 1.拿取当前FrameResource里的分配器并重置
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	// 1.2 由于管线在初始化阶段被设置为多种,所以可以自由选择哪种流水线;根据是否启用线框模式,选择把命令列表重置为哪一种流水线
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
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);
	// 3.1 清除后台缓存(渲染目标) 和 深度缓存
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	// 3.2 在管线上设置 欲渲染的目标缓存区(此处是后台缓存), 需要两个视图(RTV和DSV)的句柄(偏移查找出来)
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// 此处不再需要像上一个demo意义,依赖于CBVHeap
	//ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	//mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 4.1 命令列表设置根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// 4.2 !!这里与以往的技术不一样!
	// 绑定"RenderPassCB"所用的常数缓存,在每个"RenderPassCB"里,这段代码只需要执行一次
	// 借助函数 SetGraphicsRootConstantBufferView() 以传递参数形式把CBV和某个 "root descriptor" 相绑定
	auto passCB = mCurrFrameResource->PassCB->Resource(); // 先拿取当前FrameResource下的上传资源"PassCB"裸指针, 其实就是1个ID3D12Resouce* 
	mCommandList->SetGraphicsRootConstantBufferView(
		1,/*位于shader中的槽位*/
		passCB->GetGPUVirtualAddress()/*常数缓存的虚拟地址*/
	);
	// 5. 绘制所有已经成功加工过的渲染项, 指定要渲染的目标缓存,按给定的渲染项集 以索引进行绘制
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	// 6. 切换资源从渲染目标切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
	);
	// 6.1 结束命令记录,组建命令列表数组并添加到队列真正执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	// 7. 交换交换链里前后台呈现图像
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	// 8. 更新当前frameresource里的围栏 为 基类自增后的围栏, 把命令标记到围栏点
	mCurrFrameResource->Fence = ++mCurrentFence;
	// 8.1 向命令队列添加一条指令专门用来设置一个新的围栏点
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void LandAndWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void LandAndWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void LandAndWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void LandAndWavesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void LandAndWavesApp::UpdateCamera(const GameTimer& gt)
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

/// 更新与帧资源关联的物体CB,
void LandAndWavesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();// 拿取当前帧资源里的物体CB指针
	// 遍历每个渲染项 (单个渲染项含有单次绘制所有绘制物数据)
	for (auto& e : mAllRitems) {
		// 只要  渲染项里的脏标记(初始值等于帧资源个数3)存在  就必须对所有RenderItem执行更新 
		// 原理是 取出渲染项里的world加工一下 把它当做数据源 借助上传堆函数CopyData() 把world填充进 当前帧资源的currObjectCB里,并注销一次脏标记,对下一个渲染项进行处理
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);// 单个渲染项对应单个物体

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void LandAndWavesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);
	// 给字段PassConstants型 mMainPassCB做值
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

	// 原理是 把字段当做数据源 借助上传堆函数CopyData() 把其携带的所有数据填充进 当前帧资源的currPassCB里
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

/// 和UpdateObjctCB\UpdateMainPassCB等关联帧资源的函数类似, 每一帧中,都以此函数模拟波浪并更新"帧资源"中的波浪 "动态顶点缓存"
void LandAndWavesApp::UpdateWaves(const GameTimer& gt)
{
	// 此处数学逻辑逻辑设置每隔0.25秒 生成1个随机波浪
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f) {
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// 调用波浪对象里的Update()处理计算波浪的顶点数据
	mWaves->Update(gt.DeltaTime());

	/* 使用波浪数学方程计算出的新数据来更新"波浪动态顶点缓存"*/
	auto currWavesVB = mCurrFrameResource->WavesVB.get();// 先拿到当前帧资源内部的WavesVB的裸指针
	for (int i = 0; i < mWaves->VertexCount(); ++i)// 遍历波浪对象的每个点
	{
		// 遍历到的第i个波浪点的数据 就 填充给 1个新点v
		Vertex v;
		v.Pos = mWaves->Position(i);
		v.Color = XMFLOAT4(DirectX::Colors::Blue);
		// 把第i个新点v作为数据源, 并真正拷贝到 当前这个FrameResource里的"WavesVB"里
		currWavesVB->CopyData(i, v);
	}

	/* 最后记得给 波浪专属渲染项 里Geo管理员里的顶点数据 要被这个帧资源里的顶点数据 更新并填充*/
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void LandAndWavesApp::BuildRootSignature()
{
	/// 不再使用此前提到过的描述符表,而是改用2个Root Constant,好处就是借此摆脱CBVHeap从而直接绑定CBV

	// RootParameter可以选型为 table, root descriptor or root constants的任意一种.这里指定为根常量
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	// 直接创建2种根CBV,借助InitAsConstantBufferView辅助函数可直接创建CBV, 并指定到对应的HLSL槽位
	slotRootParameter[0].InitAsConstantBufferView(0);// 物体的CBV
	slotRootParameter[1].InitAsConstantBufferView(1);// 渲染过程CBV

	// 根签名即是一系列根参数的组合, 利用异体构造器 rootSigDesc
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	// 利用内存块来创建根签名
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()
	);
	if (errorBlob != nullptr) {
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf()))
	);
}

void LandAndWavesApp::BuildShadersAndInputLayout()
{
	// mShaders这张<string -- ID3D12Blob>无序map被填充
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");
	// 构建输入布局字段
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

/// 待栅格Geometry创建后, 可以从MeshData里获取所需顶点, 根据顶点的高度(即y坐标)把平坦的栅格变为表现山峰起伏的曲面
void LandAndWavesApp::BuildLandGeometry()
{
	/// 原理,1.先用几何生成器生成meshData,2. 开辟出专门的数据源变量, 然后用这个遍历meshData按照算法去填充顶点数据源vertices
	/// 3.预备计算1下 顶点和索引的2个数据源的字节大小 4.构造1个几何管理员geo, 并起名
	/// 5.做值过程;结合之前的数据源 给管理员geo的VertexBufferCPU/IndexBufferCPU/VertexBufferGPU/IndexBufferGPU/填充 管理员geo 的各项属性(单顶点字节偏移, 总顶点字节, 索引格式, 总索引字节)
	/// 6. 构建1个submesh并填充 然后把submesh的数据更新至管理员 geo->DrawArgs["grid"]
	/// 7.mGeometries["landGeo"] = std::move(geo); 全局几何体的map被更新
	// =========================================================
	// 先初始化一个栅格MeshData grid
	// 目的是给别人提供几何体生成器里内部那些点的meshdata
	GeometryGenerator geoGen;
	// grid就是那个一大块栅格的mesh
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// 获取我们所需顶点,并利用高度函数计算每个点的高度值
	// 点的颜色取决于它的高度,因此,最后的图像才看似如砂质的沙滩,山峰低处的植被和山峰处的积雪
	//

	// 为数据源"vertices" 开辟内存
	std::vector<Vertex> vertices(grid.Vertices.size());

	// 遍历mesh的每个顶点; 给山峰顶点集做值(填色,填位置)
	for (size_t i = 0; i < grid.Vertices.size(); ++i) {
		auto& p = grid.Vertices[i].Position;// 拿到栅格mesh的单个点位置
		vertices[i].Pos = p;// 更新数据源
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);// 单独拎出来高度y, 用设计好的高度函数计算这个高度

		// 基于顶点高度来给点上色(从山脚到山顶逐渐变色)
		if (vertices[i].Pos.y < -10.0f) {
			// 沙滩黄色
			vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
		}
		else if (vertices[i].Pos.y < 5.0f) {
			// 浅黄绿色
			vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
		}
		else if (vertices[i].Pos.y < 12.0f) {
			// 深黄绿色
			vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
		}
		else if (vertices[i].Pos.y < 20.0f) {
			// 深棕色
			vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
		}
		else {
			// 白雪皑皑
			vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
	// 结束了for之后, 此时数据源 已经被初始化,有值了,前面都是为 vertices山峰顶点集做值

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);		  // 计算数据源的 顶点缓存字节数 == 山体点数 * 单山体点字节
	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t); // 计算数据源的 索引缓存字节数 == 索引数 * 单索引字节

	// 构造1个几何管理员geo, 起名叫"landGeo"
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	// 为cpu端的几何管理员geo 顶点缓存开辟顶点Blob内存 
	// 把数据源 拷贝到 管理员geo 的cpu端去
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	// 为cpu端的几何管理员geo 索引缓存开辟顶点Blob内存 
	// 把数据源 拷贝到 管理员geo 的cpu端去
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	// 利用助手方法d3dUtil::CreateDefaultBuffer
	// 分别创建出GPU端顶点缓存,索引缓存
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
	);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
	);

	// 填充 管理员geo 的各项属性(单顶点字节偏移, 总顶点字节, 索引格式, 总索引字节)
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	// 子几何体各项属性值(实际上就是DrawIndexedIntanced函数的3个重要参数) 初始化
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	// 子几何体被 填充进管理员的 unordered_map里
	geo->DrawArgs["grid"] = submesh;

	// 截止此行,一个被填充完全的管理员geo 的所有数据都被转移至 全局表mGeometries里;此时地形看上去就像起伏山峰
	mGeometries["landGeo"] = std::move(geo);
}

void LandAndWavesApp::BuildWavesGeometryBuffers()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i) {
		for (int j = 0; j < n - 1; ++j) {
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void LandAndWavesApp::BuildPSOs()
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

void LandAndWavesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1/*1个passCB*/, (UINT)mAllRitems.size()/*objctCB数量*/, mWaves->VertexCount())/*波浪里的顶点数*/
		);
	}
}

void LandAndWavesApp::BuildRenderItems()
{
	// 1. 构建波浪的渲染项
	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	wavesRitem->ObjCBIndex = 0; // 波浪渲染项的objCB被设置为第0个
	wavesRitem->Geo = mGeometries["waterGeo"].get();// 从全局map里提出这个waterGeo
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;// Geo->DrawArgs都在此前的BuildWavesGeometryBuffers()里用submesh做过值
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();// 波浪渲染项的裸指针

	mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());// 把波浪渲染项填到一个渲染项数组mRitemLayer里
	// 2. 构建栅格(其实就是山峰)的渲染项
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 1;// 山峰渲染项的ObjCB被设置为第1个
	gridRitem->Geo = mGeometries["landGeo"].get();;// 从全局map里提出这个landGeo
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;// Geo->DrawArgs都在此前的BuildLandGeometryBuffers()里用submesh做过值
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());// 把山峰渲染项填到一个渲染项数组mRitemLayer里

	mAllRitems.push_back(std::move(wavesRitem));// 全局渲染项
	mAllRitems.push_back(std::move(gridRitem));
}

void LandAndWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));// 把 物体CB结构体 255归一化
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();// 取得 当前帧资源里物体CB的裸指针, ID3D12Resource* 型

	// 遍历所有指定的渲染项 对于每个指定的渲染项而言,需要作出调整
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];// 第i个渲染项

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView()); //绑定渲染项里 Geo管理员的VertexBufferView到管线
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());			 //绑定渲染项里 Geo管理员的IndexBufferView到管线
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);				 //绑定渲染项里 图元类型到管线

		// 先拿到 物体CB的 虚拟地址
		// 把虚拟地址 偏移到 渲染项里缓存区序号 * 单物体常数字节
		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
		objCBAddress += ri->ObjCBIndex * objCBByteSize;

		// 此处是 SetGraphicsRootConstantBufferView函数 以传递参数形式把物体CBV和某个root descriptor相绑定
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		// 按渲染项绘制
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

float LandAndWavesApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 LandAndWavesApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}
