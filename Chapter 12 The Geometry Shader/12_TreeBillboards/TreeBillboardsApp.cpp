//***************************************************************************************
// TreeBillboardsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

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

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();// 渲染项里的材质变换矩阵

	int NumFramesDirty = gNumFrameResources;// 渲染项里的帧脏标记,会发生改变

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;// 渲染项里使用的材质
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;// 渲染项里的图元类型

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

// 渲染分层枚举; 给构建渲染项时候分层归类用
enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class TreeBillboardsApp : public D3DApp
{
public:
	TreeBillboardsApp(HINSTANCE hInstance);
	TreeBillboardsApp(const TreeBillboardsApp& rhs) = delete;
	TreeBillboardsApp& operator=(const TreeBillboardsApp& rhs) = delete;
	~TreeBillboardsApp();

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
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayouts();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildTreeSpritesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;// 帧资源智能指针集
	FrameResource* mCurrFrameResource = nullptr;// 当前帧资源指针
	int mCurrFrameResourceIndex = 0;// 当前帧资源序数

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;// 根签名字段

	UINT mCbvSrvDescriptorSize = 0;// 邻接CBV或者SRV增量
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;// 字段:SRV 视图HEAP

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;// 全局几何体表
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;// 全局材质表
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;// 全局纹理表
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;        // 全局shader表
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;// 全局PSO表

	std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;       // 标准用,通用的顶点的输入布局
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;// 公告牌四边形顶点的输入布局

	RenderItem* mWavesRitem = nullptr;// 全局波浪渲染项字段,用以承接某一时刻外部给的数据

	// 全局渲染项数组
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// 全局渲染项层级; 受制于PSO而划分的 一组渲染项集, 与渲染项分层有关
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	// 波浪,供数学计算用或者模拟几何体用,帧资源构造器里也用到它
	std::unique_ptr<Waves> mWaves;

	// 有1个主PASS常数缓存
	PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };// 眼睛位置
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();// 浮点型透视投影矩阵

	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

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
		TreeBillboardsApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

TreeBillboardsApp::TreeBillboardsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TreeBillboardsApp::~TreeBillboardsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TreeBillboardsApp::Initialize()
{
	// 先检查框架基类初始化
	if (!D3DApp::Initialize())
		return false;

	// 重置命令列表以准备初始化命令
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	// CBV、SRV的邻接增量字段初始化
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// 初始化的时候就构建1个波浪涟漪实例
	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	LoadTextures();// 创建出各个物体的纹理并把资源注册到全局纹理表

	BuildRootSignature();// 根参数初始化后创建出需要的根签名
	BuildDescriptorHeaps();// 创建SRV HEAP之后利用句柄偏移分别创建出 草里纹理、水纹理、铁丝网纹理、树木公告牌纹理数组的纹理这4个SRV视图
	BuildShadersAndInputLayouts();// 设计几个特效启用宏数组.并注册全局shader表和字段用的2个输入布局

	BuildLandGeometry();// 首先利用栅格mesh(几何生成器造出来的)构建出山峰数据源,然后构建1个geo,给geo一系列做值(包括顶点/索引缓存),最后在全局几何表里注册; mGeometries["landGeo"]
	BuildWavesGeometry();// 类似山峰,最后全局几何表里注册波浪的点数据 mGeometries["waterGeo"]
	BuildBoxGeometry();// 类似山峰,全局注册表里最后注册铁丝网盒子的数据 mGeometries["boxGeo"];
	BuildTreeSpritesGeometry();// 类似山峰 全局注册表里最后注册公告牌树木的数据 mGeometries["treeSpritesGeo"],但是这里使用的是自定义的顶点结构体TreeSpriteVertex;

	BuildMaterials(); // 构建出草地 / 水 / 铁丝网 / 公告牌的材质, 并在全局材质表注册

	BuildRenderItems();// 构建波浪/山峰/盒子/树木的渲染项并刷新各自的层级,给字段里新增各自的渲染项,最后同时也注册一下渲染项数组
	BuildFrameResources();// 调用构造器来构建3个帧资源;
	BuildPSOs();//  构建出4种不同的管线(非透明,透明,阿尔法测试,公告牌);并更新了全局PSO表

	// 执行完上述各初始化步骤后, 构建命令列表数组并在队列里执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 强制CPU等待GPU
	FlushCommandQueue();

	return true;
}

void TreeBillboardsApp::OnResize()
{
	D3DApp::OnResize();

	// 由于窗口发生尺寸改变, 故需要重建透视投影矩阵
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);// 更新矩阵到字段
}

void TreeBillboardsApp::Update(const GameTimer& gt)
{
	// 键盘按键事件,目前暂无
	OnKeyboardInput(gt);
	// 每帧调整眼睛观察位置:每帧重构视图矩阵
	UpdateCamera(gt);

	// 每帧刷新 当前帧用的 帧资源和帧资源索引
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// 强制令CPU等待至GPU处理完当前帧资源里围栏前所有命令
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);// 每帧更新材质的纹理坐标,模拟水流的动画; (每帧更新水流材质的材质变换矩阵)
	UpdateObjectCBs(gt);// 自定义1个"物体常量"数据源拷贝到 遍历所有渲染项过程中 单个渲染项里的对应序数的物体里去
	UpdateMaterialCBs(gt);// 利用遍历的每个材质的材质变换矩阵; 把数据源 "材质常量" 拷贝到 当前帧资源下的 对应序数的材质里去
	UpdateMainPassCB(gt);// 每帧都把这个 主PASS CB数据 拷贝到 当前帧的Pass里去
	UpdateWaves(gt);// 每一帧中,都以此函数模拟波浪顶点位置并更新"帧资源"中的波浪 "动态顶点缓存",算法比较特殊!!!!!
}

void TreeBillboardsApp::Draw(const GameTimer& gt)
{
	// 1.拿取当前帧资源里的分配器并重置
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	// 2. 复用命令列表,PSO刷新为非透明管线
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	// 3. 设置视口和裁剪矩形
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// 依据资源的使用用途 指示其状态切换,此处把资源(后台缓存)从呈现切换为渲染目标状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// 清除后台缓存(渲染目标) 和 深度缓存 2种视图
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// 在管线上设置 欲渲染的目标缓存区(此处是后台缓存), 需要两个视图(RTV和DSV)的句柄(偏移查找出来)
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());


	// 用SRV视图堆构建1个堆数组, 把视图堆数组绑定到管线上
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	// 管线上绑定根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	// 绑定"RenderPassCB"所用的常数缓存,在每个"RenderPassCB"里,这段代码只需要执行一次
	// 借助函数 SetGraphicsRootConstantBufferView() 以传递参数形式把CBV和某个 "root descriptor" 相绑定
	auto passCB = mCurrFrameResource->PassCB->Resource();// 先拿取当前FrameResource下的上传资源"PassCB"裸指针, 其实就是1个ID3D12Resouce*
	mCommandList->SetGraphicsRootConstantBufferView(
		2, /*位于着色器中的槽位*/
		passCB->GetGPUVirtualAddress()/*常数缓存的虚拟地址*/
	);

	// 绘制出层级为非透明物体的渲染项(山峰)
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	// 切换管线为阿尔法管线; 并绘制出层级为阿尔法测试的渲染项(铁丝网盒子)
	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);
	// 切换管线为公告板技术管线,并绘制出层级为阿尔法测试公告牌的渲染项(那些树木)
	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);
	// 切换管线为透明管线,绘制出层级为透明的渲染项(水体波浪)
	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

	// 切换资源从渲染目标切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
	);

	// 结束命令记录,组建命令列表数组并添加到队列真正执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 交换交换链里前后台呈现图像
	ThrowIfFailed(mSwapChain->Present(0, 0));
	// 刷新当前用的后台缓存
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	// 更新当前frameresource里的围栏 为 基类自增后的围栏, 把命令标记到围栏点
	mCurrFrameResource->Fence = ++mCurrentFence;
	// 向命令队列添加一条指令专门用来设置一个新的围栏点
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TreeBillboardsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void TreeBillboardsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void TreeBillboardsApp::OnMouseMove(WPARAM btnState, int x, int y)
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

/// 键盘按键事件
void TreeBillboardsApp::OnKeyboardInput(const GameTimer& gt)
{
}

/// 每帧调整眼睛观察位置:每帧重构视图矩阵
void TreeBillboardsApp::UpdateCamera(const GameTimer& gt)
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

void TreeBillboardsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();// 取出水流材质

	// 暂存初始情况下的 水流材质的纹理变换矩阵
	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);
	// 让其随时间发生周期性变化
	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();
	// 阈值限定
	if (tu >= 1.0f)
		tu -= 1.0f;
	if (tv >= 1.0f)
		tv -= 1.0f;

	// 每帧更新水流材质的材质变换矩阵
	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// 材质变化了,命中脏标记;需要更新材质CBUFFER
	waterMat->NumFramesDirty = gNumFrameResources;
}

/// 自定义1个"物体常量"数据源拷贝到 遍历所有渲染项过程中 单个渲染项里的对应序数的物体里去
void TreeBillboardsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();// 首先拿到当前帧资源里ObjectCB裸指针
	// 遍历渲染项数组(单个渲染项含有单次绘制所有绘制物数据)
	// e是每次的渲染项
	for (auto& e : mAllRitems) {
		// 只要常量发生改变, 帧脏标记就必须对所有RenderItem执行更新
		if (e->NumFramesDirty > 0) {

			XMMATRIX world = XMLoadFloat4x4(&e->World);// 加载出单个渲染项里的世界变换矩阵
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);// 加载出单个渲染项里的材质变换矩阵

			// 用上面2个信息自定义1个"物体常量"数据源
			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			// 把数据源ObjConstants拷贝到 当前帧资源下的 单个渲染项里的对应序数的物体里去
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// 对下一个FrameResource执行更新
			e->NumFramesDirty--;
		}
	}
}

/// 利用遍历的每个材质的材质变换矩阵; 把数据源 "材质常量" 拷贝到 当前帧资源下的 对应序数的材质里去
void TreeBillboardsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	// 遍历全局材质表里的材质
	for (auto& e : mMaterials) {
		Material* mat = e.second.get();// 得到单个材质的裸指针
		// 检查材质里的帧脏标记
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);// 暂存单个材质的材质变换矩阵
			// 使用其自定义1个"材质常量"数据源
			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
			// 把数据源 "材质常量" 拷贝到 当前帧资源下的 对应序数的材质里去
			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

/// 每帧都把这个 主PASS CB数据 拷贝到 当前帧的Pass里去
void TreeBillboardsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

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
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };// 每帧都设定环境光为这个颜色
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();// 暂存当前帧资源下的 PASS常量
	currPassCB->CopyData(0, mMainPassCB);// 每帧都把这个 主PASS CB数据 拷贝到 当前帧的Pass里去
}

/// 每一帧中,都以此函数模拟波浪并更新"帧资源"中的波浪 "动态顶点缓存",算法比较特殊
void TreeBillboardsApp::UpdateWaves(const GameTimer& gt)
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

	// 每帧调用波浪对象里的Update()处理计算波浪的顶点数据
	mWaves->Update(gt.DeltaTime());

	/* 使用波浪数学方程计算出的新数据来更新"波浪动态顶点缓存"*/
	auto currWavesVB = mCurrFrameResource->WavesVB.get();// 暂存当前帧的 波浪顶点缓存
	for (int i = 0; i < mWaves->VertexCount(); ++i) {
		// 遍历到的第i个波浪点的数据 就 填充给 1个新点v
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		// Derive tex-coords from position by mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();
		// 把第i个新点v作为数据源, 并真正拷贝到 当前这个FrameResource里的"WavesVB"里
		currWavesVB->CopyData(i, v);
	}

	/* 最后记得给 波浪专属渲染项 里Geo管理员里的顶点数据 要被这个帧资源里的顶点数据 更新并填充*/
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

/// 创建出各个物体的纹理并把资源注册到全局纹理表
void TreeBillboardsApp::LoadTextures()
{
	// 创建草地纹理
	auto grassTex = std::make_unique<Texture>();						// 1个Texture唯一指针,有一份纹理
	grassTex->Name = "grassTex";										// 起名叫"grassTex"
	grassTex->Filename = L"../../Textures/grass.dds";					// 文件来源是"L"../../Textures/grass.dds"
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), // 结合uploaderHeap创建出真正的纹理
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap)
	);
	// 创建水纹理
	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap)
	);
	// 创建铁丝网纹理
	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"../../Textures/WireFence.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap)
	);
	// 创建公告牌使用的 树木纹理数组
	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	// 这个文件是通过微软的texassemble工具把4个.dds合并成一个treeArray2的纹理数组.dds
	// 若给定纹理数组的索引及其mipmap层级,则可以找到第i个数组切片,第j个mip切片处的子资源
	treeArrayTex->Filename = L"../../Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap)
	);

	// 全局纹理表里注册这些 纹理nique_ptr资源
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

/// 根参数初始化后创建出需要的根签名
void TreeBillboardsApp::BuildRootSignature()
{
	// 声明一个根签名用的纹理table,初始化为SRV型
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	// 声明持有4个元素的根参数数组
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	// 根参数分别被选型初始化;从使用频率由高到低排序
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);// 0号是1个table
	slotRootParameter[1].InitAsConstantBufferView(0);// 1号是根常量
	slotRootParameter[2].InitAsConstantBufferView(1);// 2号也是根常量
	slotRootParameter[3].InitAsConstantBufferView(2);// 3号也是根常量
	// 构建持有6个元素的静态采样器数组
	auto staticSamplers = GetStaticSamplers();
	// 填充rootSigDesc
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	// 结合Blob内存块创建出需要的根签名
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
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

/// 创建SRV HEAP之后利用句柄偏移分别创建出 草里纹理、水纹理、铁丝网纹理、树木公告牌纹理数组的纹理这4个SRV视图
void TreeBillboardsApp::BuildDescriptorHeaps()
{
	// 填充SRV VIEW HEAP描述,并创建出持有4个视图的SRV HEAP
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;// 有4个SRV 视图
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// 暂存SRV HEAP里的视图句柄,负责偏移用
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// 从全局纹理表李取出所有的纹理,均是一份D3D12Resource
	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	// 填充SRV视图的 描述并创建出草地纹理的SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // 采样时,返回的坐标上的向量
	srvDesc.Format = grassTex->GetDesc().Format;								// 格式先指定为草地纹理
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;						// 资源维数
	srvDesc.Texture2D.MostDetailedMip = 0;										// 最大的mipmap层级 0~MipLevels-1
	srvDesc.Texture2D.MipLevels = -1;											// 若设为-1,则表示从MostDetailedMip到最后1个mipmap之间的所有mipmap
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄并重建为水纹理格式,创建出水纹理SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);
	//  偏移到下一个句柄并重建为铁丝网纹理格式,创建出铁丝网纹理SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);
	//  偏移到下一个句柄并重建为树木纹理数组 格式,创建出树木纹理数组 纹理SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;// 资源维数重建为纹理数组,不再是2D纹理了
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;									// 重建
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;// 重建为纹理数组大小
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

/// 设计几个特效启用宏数组.并注册全局shader表和字段用的2个输入布局
void TreeBillboardsApp::BuildShadersAndInputLayouts()
{
	// 自定义1个关联 雾效 的shader宏数组
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};
	// 自定义1个关联阿尔法值测试的 shader宏数组
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	/// 往全局shader表(实际上就是Blob内存块)里注册一些 shader
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");// 标准情况下用的 VS
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_0");  // 非透明物体用的 PS,附注:启动了雾气特效
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");// 阿尔法测试用的PS,附注:启动了阿尔法测试(clip裁剪像素)
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");// 树木公告牌用的VS;
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");// 树木公告牌用的GS;把Shaders\\TreeSprite.hlsl中名为GS的几何着色器编译为字节码
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");// 树木公告牌用的PS,附注:启动了阿尔法测试(clip裁剪像素)

	/// 自定义值更新 字段里的输入布局mStdInputLayout(山峰用), mTreeSpriteInputLayout(树木用)
	mStdInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

/// 首先利用栅格mesh(几何生成器造出来的)构建出山峰数据源,然后构建1个geo,给geo一系列做值(包括顶点/索引缓存),最后在全局几何表里注册; mGeometries["landGeo"]
void TreeBillboardsApp::BuildLandGeometry()
{
	// 使用几何生成器生成"栅格"的mesh,即此处的grid
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);
	// 开辟出一个山峰用的顶点集,即这里的山峰点集"vertices"
	std::vector<Vertex> vertices(grid.Vertices.size());
	// 遍历取出栅格mesh里每个点的有关属性,给山峰点集vertices做值
	for (size_t i = 0; i < grid.Vertices.size(); ++i) {
		auto& p = grid.Vertices[i].Position;// 暂存mesh里的单个点的位置
		vertices[i].Pos = p;// 山峰点位置 做值
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);// 山峰点y 更新为: 单独拎出来高度y, 用设计好的高度函数计算这个高度,高度y是变化的
		vertices[i].Normal = GetHillsNormal(p.x, p.z);// 山峰点的法线 计算法线,为了后续的光照做准备
		vertices[i].TexC = grid.Vertices[i].TexC;    // 山峰点的纹理坐标 更新为:栅格mesh里点的纹理坐标
	}
	// =====结束了for之后, 此时数据源 已经被初始化,有值了,前面都是为 vertices山峰顶点集做值====

	// 把山峰点集作为数据源; 暂存一下山峰数据源的 vertex/index size
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);// 计算山峰数据源的 顶点缓存字节数 == 山体点数 * 单山体点字节
	std::vector<std::uint16_t> indices = grid.GetIndices16();      // 取出栅格mesh里的索引数组 构建1个山峰索引数组
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);// 计算山峰数据源的 索引缓存字节数 == 索引数 * 单索引字节

	// 临时构建1个集合管理员Geo,并取名叫"landGeo"
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	// 为cpu端的几何管理员geo 顶点缓存开辟顶点Blob内存 
	// 把山峰数据源(顶点集) 拷贝到 管理员geo 的cpu端去(实际上就是给geo的这边CPU端做值)
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	// 为cpu端的几何管理员geo 索引缓存开辟顶点Blob内存 
	// 把山峰数据源(索引集) 拷贝到 管理员geo 的cpu端去
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	// 利用助手方法d3dUtil::CreateDefaultBuffer,结合山峰数据源(顶点集\索引集)和中介Uploader
	// 分别创建出GPU端顶点缓存,索引缓存(实际上就是给geo的这边GPU端做值)
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader
	);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader
	);
	// 接着就是继续给geo管理员里的其他属性做值
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	// 自定义1个submesh并使用它给geo里的subMeshGeometry做值
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["grid"] = submesh;

	// 最后全局几何体表注册一下 山峰geo的所有信息
	mGeometries["landGeo"] = std::move(geo);
}

/// 类似山峰,最后全局几何表里注册波浪的点数据 mGeometries["waterGeo"]
void TreeBillboardsApp::BuildWavesGeometry()
{
	// 开辟出波浪索引集 indices
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount());
	assert(mWaves->VertexCount() < 0x0000ffff);

	// 给每个方格插值并计算
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
	// ======此时波浪索引集已经具备数据了===========

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);      // 波浪顶点字节大小
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);// 波浪索引字节大小

	// 构建1个geo管理员并做值
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";
	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;
	// 只给geo管理员的CPU端索引 拷贝数据源
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	// geo管理员的 索引缓存构造完毕
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
	// geo其他属性做值
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["grid"] = submesh;
	// 最后全局几何体表注册一下 波浪geo的所有信息
	mGeometries["waterGeo"] = std::move(geo);
}

/// 类似山峰,全局注册表里最后注册铁丝网盒子的数据 mGeometries["boxGeo"];
void TreeBillboardsApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i) {
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
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
	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

/// 类似山峰 全局注册表里最后注册公告牌树木的数据 mGeometries["treeSpritesGeo"],但是这里使用的是自定义的顶点结构体TreeSpriteVertex;
void TreeBillboardsApp::BuildTreeSpritesGeometry()
{
	// 公告牌专用顶点类型
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;// 表示公告牌的宽和高
	};
	// 开辟出含16个元素的公告牌顶点集
	static const int treeCount = 16;///!!!!!这儿的treeCount和下面的数组决定了场景里随机出现多少棵树
	std::array<TreeSpriteVertex, 16> vertices;// 开辟出含16个元素的公告牌顶点集 vertices
	// 给公告牌顶点集 vertices做值
	for (UINT i = 0; i < treeCount; ++i) {
		float x = MathHelper::RandF(-45.0f, 45.0f);// 随机指定1个位置
		float z = MathHelper::RandF(-45.0f, 45.0f);// 随机指定1个位置
		float y = GetHillsHeight(x, z);// 独拎出来高度y, 用设计好的高度函数计算这个高度,高度y是变化的

		// 稍稍给树木高度抬高一点,使之高于地面
		y += 8.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);// 填充树木顶点集vertices的单个点
		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	}

	// 自定义1个公告牌用的 索引集(含有16个数) indices,指定绕序
	std::array<std::uint16_t, 16> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15
	};

	// 暂存顶点和索引字节大小
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	// 给管理员geo做值过程
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["points"] = submesh;

	// 全局几何表里注册 mGeometries["treeSpritesGeo"]
	mGeometries["treeSpritesGeo"] = std::move(geo);
}

/// 构建出4种不同的管线(非透明,透明,阿尔法测试,公告牌);并更新了全局PSO表
void TreeBillboardsApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;// 声明1个流水线opaquePsoDesc

	/// 非透明物体专用PSO
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };// 标准用输入布局
	opaquePsoDesc.pRootSignature = mRootSignature.Get();// 根签名
	opaquePsoDesc.VS =// 指定并设置如下信息的 顶点shader
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =// 指定并设置如下信息的 像素shader
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);// 默认光栅状态
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);		   // 默认混合状态
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);// 默认深度模板状态
	opaquePsoDesc.SampleMask = UINT_MAX;									// 多重采样样本数
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;// 三角形图元
	opaquePsoDesc.NumRenderTargets = 1;										// 同时所用的渲染目标数量:1
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;						// 渲染目标[0]的格式,用后台缓存格式
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;					// 采样数量
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;// 采样质量
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;                           // 深度模板格式: 字段深度模板
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));// 创建出非透明流水线,注册进全局PSO


	/// 透明物体专用PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;// transparentPsoDesc这个透明流水线,信息先继承自非透明的管线
	// 手动填充1个"渲染目标"的"混合状态"
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;					// 常规混合功能开关,禁止同时与LogicOpEnable一起开启					
	transparencyBlendDesc.LogicOpEnable = false;				// 逻辑混合功能开关,禁止同时与BlendEnable一起开启
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;		// 指定RGB混合中的 源混合因子
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;// 指定RGB混合中的 目标混合因子
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;			// 指定RGB混合中的 混合运算符
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;		// 指定Alpha混合中 源混合因子
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;	// 指定Alpha混合中 目标混合因子
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;	// 指定alpha混合中 混合运算符
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;		// 指定源颜色与目标颜色使用的逻辑运算符
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;// 控制可被写入后台缓存的哪些颜色通道
	// 重建透明管线的混合状态 为上述填好的 描述状态
	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));// 创建出透明管线,注册进全局PSO


	/// Alpha测试用 专用PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;// 阿尔法测试用管线先继承自原非透明管线
	// 重建阿尔法管线的像素shader
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	// 重建阿尔法管线的光栅裁剪模式
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));// 创建出阿尔法测试管线,注册进全局PSO

	/// 公告牌专用PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;// 公告牌管线先默认继承自原非透明管线
	treeSpritePsoDesc.VS =// 重建其顶点shader
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS = // 重建其几何shader; GS也要指定为流水线上的对象的一部分
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =// 重建其像素shader
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;// 重建树木的图元指定为点,不再是原先的三角形
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };// 重建输入布局是公告牌专用 输入布局,不再是原先的标准布局
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;// 重建光栅裁剪
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));// 创建出公告牌管线,注册进全局PSO
}

/// 调用构造器来构建3个帧资源;
void TreeBillboardsApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount())
		);
	}
}

/// 构建出草地/水/铁丝网/公告牌的材质,并在全局材质表注册
void TreeBillboardsApp::BuildMaterials()
{
	// 草地材质
	auto grass = std::make_unique<Material>();// 构建草地材质唯一指针
	grass->Name = "grass";// 起名叫"grass"						
	grass->MatCBIndex = 0;// 材质索引登记为 0号
	grass->DiffuseSrvHeapIndex = 0;// 材质里要使用的漫反射纹理索引 登记为0号
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// 自定义漫反射反照率
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);// 自定义菲涅尔系数R0
	grass->Roughness = 0.125f;// 自定义粗糙度

	// 水材质; 我们需要的工具(透明度，环境反射)，所以我们现在尽可能模拟它。
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;// 材质索引登记为 1号
	water->DiffuseSrvHeapIndex = 1;// 材质里要使用的漫反射纹理索引 登记为1号
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);// 水阿尔法值(影响不透明度)设为0.5
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	// 盒子的铁丝网材质
	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;// 材质索引登记为 2号
	wirefence->DiffuseSrvHeapIndex = 2;// 材质里要使用的漫反射纹理索引 登记为2号
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wirefence->Roughness = 0.25f;

	// 公告牌树木的材质
	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 3;// 材质索引登记为 3号
	treeSprites->DiffuseSrvHeapIndex = 3;// 材质里要使用的漫反射纹理索引 登记为3号
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	// 全局材质表注册它们
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["treeSprites"] = std::move(treeSprites);
}

/// 构建波浪/山峰/盒子/树木的渲染项并刷新各自的层级,给字段里新增各自的渲染项,最后同时也注册一下渲染项数组
void TreeBillboardsApp::BuildRenderItems()
{
	/// 波浪用的渲染项
	auto wavesRitem = std::make_unique<RenderItem>();// 构建1个波浪渲染项唯一指针
	wavesRitem->World = MathHelper::Identity4x4();// 设定波浪的世界矩阵为单位矩阵
	// XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(1, 1, 1) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));// 初始化一个矩阵填充box渲染项的世界矩阵
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));// 设定 纹理变换矩阵也为默认单位矩阵
	wavesRitem->ObjCBIndex = 0;// 设定 波浪这个物体 物体索引为 0号
	wavesRitem->Mat = mMaterials["water"].get();// 设定 波浪的材质是全局材质表里的mMaterials["water"]
	wavesRitem->Geo = mGeometries["waterGeo"].get();// 设定 影响波浪的几何体形状的管理员Geo是 mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;// 设定 波浪的图元类型是三角形列表
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;// 设定DrawIndexedInstanced三项渲染参数为 管理员里的数据
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	// 全局波浪渲染项字段被刷新
	mWavesRitem = wavesRitem.get();
	// 刷新全局渲染项层级, 给这个字段里新增"波浪渲染项"
	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	/// 栅格(山峰)用渲染项
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));// 纹理坐标按5x5反复
	gridRitem->ObjCBIndex = 1;// 设定渲染项的物体索引为 1号
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	// 刷新全局渲染项层级, 给这个字段里新增"山峰(栅格)渲染项"
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	/// 铁丝网盒子用渲染项
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	boxRitem->ObjCBIndex = 2;// 设定为2号
	boxRitem->Mat = mMaterials["wirefence"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	// 刷新全局渲染项层级, 给这个字段里新增"铁丝网盒子渲染项"
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());

	/// 公告牌树木用渲染项
	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = 3;// 设定为3号
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	// 刷新全局渲染项层级, 给这个字段里新增"树木渲染项"
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

	// 同时注册一下全局渲染项数组
	mAllRitems.push_back(std::move(wavesRitem));
	mAllRitems.push_back(std::move(gridRitem));
	mAllRitems.push_back(std::move(boxRitem));
	mAllRitems.push_back(std::move(treeSpritesRitem));
}

void TreeBillboardsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	// 物体常量和材质常量 255字节对齐, 单字节大小
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
	// 暂存"当前帧资源下"物体常量和材质常量的D3D12Resouce指针
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// 遍历外部设定的渲染项数组
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];// 第i个渲染项

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());// 管线上绑定 顶点缓存
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());			// 管线上绑定 索引缓存
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);				// 管线上绑定图元类型

		// 把SRV句柄执行偏移, 偏移序数为渲染项里使用的材质的漫反射纹理序数
		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;// 物体CB地址就是 当前帧的物体CB地址加上渲染项的物体索引*单物体大小
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;// 材质CB地址就是 当前帧的材质CB地址加上渲染项的物体索引*单材质大小

		cmdList->SetGraphicsRootDescriptorTable(0, tex);// 管线上绑定纹理视图, 0号
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);//管线上绑定 物体常量视图,1号,2号是PASS,在外部	Draw()里绑定
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);//管线上绑定 材质常量视图,3号

		// 按索引绘制
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

/// 构建持有6个元素的静态采样器数组
std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TreeBillboardsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

float TreeBillboardsApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 TreeBillboardsApp::GetHillsNormal(float x, float z)const
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
