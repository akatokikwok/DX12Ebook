//***************************************************************************************
// InstancingAndCullingApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;// 默认定义3个帧资源

/// 实例化技术需要用到的渲染项
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();// 某物体要用到的世界变换矩阵

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();// 某物体要用到的纹理变换矩阵

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;// Update入口里会用到的更新标志

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;// ObjectCB渲染项要画的第几个物体(即GPU中常数缓存的索引)

	Material* Mat = nullptr;// 本此渲染项的材质
	
	MeshGeometry* Geo = nullptr;// 本次渲染项的几何体

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	/* 这是新增的2个变量*/
	BoundingBox Bounds;// 单个渲染项里的包围体; DirectxCollison库里提供的包围盒,是盒子中心与扩展向量组合的表达形式,公式是c=0.5(Vmin+Vmax), e=0.5(Vmax-Vmin)
	std::vector<InstanceData> Instances;// 本次渲染项里持有的一组实例化数据; InstanceData等价于ObjectConstants; 渲染项里持有的一组实例(允许大容量); 渲染项里持有实例化次数

	// 绘制三参数(但本工程再补1个 "要被实例化技术操作的实例数量").
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	UINT InstanceCount = 0;// 实例数量; 可能在Update入口里更新
};

class InstancingAndCullingApp : public D3DApp
{
public:
	InstancingAndCullingApp(HINSTANCE hInstance);
	InstancingAndCullingApp(const InstancingAndCullingApp& rhs) = delete;
	InstancingAndCullingApp& operator=(const InstancingAndCullingApp& rhs) = delete;
	~InstancingAndCullingApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateInstanceData(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildSkullGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;// 帧资源数组
	FrameResource* mCurrFrameResource = nullptr;				// 当前帧资源
	int mCurrFrameResourceIndex = 0;							// 当前帧资源序数

	UINT mCbvSrvDescriptorSize = 0;// 邻接尺寸,用以偏移CPU句柄用(详见BuildDescriptorHeaps函数)
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;// 根签名
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;// 用来放SRV的堆(详见BuildDescriptorHeaps函数)

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;// 全局几何体表
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;	   // 全局材质表
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;	   // 全局贴图表
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;				   // 全局Shader表
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;		   // 全局管线表
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;						   // 输入布局
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;					   // 全局渲染项

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;// "非透明"渲染层级组

	UINT mInstanceCount = 0;// 采用实例化技术处理的实例数量
	bool mFrustumCullingEnabled = true;
	BoundingFrustum mCamFrustum;// 相机视锥体

	PassConstants mMainPassCB;// 主Pass;目前仅1个主PASS,日后可能会增加阴影Pass

	Camera mCamera;

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
		InstancingAndCullingApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

InstancingAndCullingApp::InstancingAndCullingApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

InstancingAndCullingApp::~InstancingAndCullingApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool InstancingAndCullingApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, so we have to query this information.
	// 获取cbv描述符增量更新的尺寸
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);// 摄像机给个初始位置

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildSkullGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void InstancingAndCullingApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	// 根据投影矩阵反过来计算观察空间里视锥体的函数如下
	BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());
}

void InstancingAndCullingApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateInstanceData(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void InstancingAndCullingApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// 用SRV视图堆构建1个堆数组, 把视图堆数组绑定到管线上
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	// 管线上绑定根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	// 绑定此场景所需的全部材质,对于结构化材质而言,可以跳过描述符HEAP而将其直接设置到1个 root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(1, matBuffer->GetGPUVirtualAddress());// 材质结构化buffer在管线上绑定到1号
	// PASSCB在管线上绑定到2号
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());// PASSCB在管线上绑定到2号
	// 场景里所有使用的纹理贴图 绑定到3号
	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());// 场景里所有使用的纹理贴图 绑定到3号
	// 注意,本工程里,只分配了仅1个渲染项,所以仅用了1个实例buffer
	// 假如想让实例拥有多个渲染项,则需要为每个渲染项都添加一个结构化buffer,并为其分配出足够大的空间容纳最大数量的实例
	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void InstancingAndCullingApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void InstancingAndCullingApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void InstancingAndCullingApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0) {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void InstancingAndCullingApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(20.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-20.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-20.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(20.0f * dt);

	if (GetAsyncKeyState('1') & 0x8000)
		mFrustumCullingEnabled = true;

	if (GetAsyncKeyState('2') & 0x8000)
		mFrustumCullingEnabled = false;

	mCamera.UpdateViewMatrix();
}

void InstancingAndCullingApp::AnimateMaterials(const GameTimer& gt)
{

}

void InstancingAndCullingApp::UpdateInstanceData(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();									  // 暂存相机观察矩阵
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view); // 暂存相机观察矩阵的逆矩阵

	auto currInstanceBuffer = mCurrFrameResource->InstanceBuffer.get();// 当前帧的实例buffer
	// 遍历所有渲染项
	for (auto& e : mAllRitems) {
		// 拿到但个渲染项里所有的实例次数
		const auto& instanceData = e->Instances;
		// 有一个计数器,暗示第几个缓存区
		// 来确保结构化buffer前面的数据均为可见实例;而实例与视锥体相交就会被加入结构化buffer的下一个空槽位
		// 要将visibleInstanceCount挪到遍历allRitems循环外，这样才能排序所有渲染项实例数据
		// 如果有多个渲染项，如果InstanceIndex在循环内，后面数据将会覆盖掉前面的。
		int visibleInstanceCount = 0;

		// 遍历单个渲染项的所有实例
		for (UINT i = 0; i < (UINT)instanceData.size(); ++i) {
			XMMATRIX world        = XMLoadFloat4x4(&instanceData[i].World);		  // 暂存单个实例的world
			XMMATRIX texTransform = XMLoadFloat4x4(&instanceData[i].TexTransform);// 暂存单个实例的TexTransform

			XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);// 暂存一下world的逆矩阵
			XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);				// 暂存一下viewToWorld

			/// 把摄像机的视锥体从 观察空间变换至单个实例的局部空间(因为骷髅头MESH的ABB盒子位于局部空间)
			BoundingFrustum localSpaceFrustum;
			mCamFrustum.Transform(localSpaceFrustum, viewToLocal);

			/// 在局部空间执行 包围体和视锥的相交测试
			// !!!!如若关闭视锥体裁剪或者查到包围体仍在视锥内,则会进到这个if里执行逻辑
			// if里的逻辑就是"不执行剔除",仍然拷贝数据源到结构体buffer里,会导致实例数量增多
			if ((localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT/*包围体位于视锥体之外*/) || 
				(mFrustumCullingEnabled == false)) {
				InstanceData data;
				XMStoreFloat4x4(&data.World,        XMMatrixTranspose(world));
				XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
				data.MaterialIndex = instanceData[i].MaterialIndex;

				// 把上面构建的数据源data,也就是可见实例的数据, 拷贝到对应序数的结构化buffer里
				// //将实例数据一个个地拷贝至GPU缓存; //复制完实例数据，递增到下一个实例
				currInstanceBuffer->CopyData(visibleInstanceCount++, data);
			}
		}
		// 查完所有骷髅头实例后, 更新渲染项里的 实例数量
		e->InstanceCount = visibleInstanceCount;

		// 更新每帧 统计有多少个实例在被剔除操作后仍然可见
		std::wostringstream outs;
		outs.precision(6);
		outs << L"实例化和裁剪工程里" <<
			L"目前有:    " << e->InstanceCount <<
			L" 个物体可以被看到 out of " << e->Instances.size();
		mMainWndCaption = outs.str();// 打印到窗口标题上
	}
}

void InstancingAndCullingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mMaterials) {
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void InstancingAndCullingApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

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
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void InstancingAndCullingApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"../../Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"../../Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"../../Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	auto crateTex = std::make_unique<Texture>();
	crateTex->Name = "crateTex";
	crateTex->Filename = L"../../Textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), crateTex->Filename.c_str(),
		crateTex->Resource, crateTex->UploadHeap));

	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"../../Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));

	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto defaultTex = std::make_unique<Texture>();
	defaultTex->Name = "defaultTex";
	defaultTex->Filename = L"../../Textures/white1x1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), defaultTex->Filename.c_str(),
		defaultTex->Resource, defaultTex->UploadHeap));

	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[crateTex->Name] = std::move(crateTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[defaultTex->Name] = std::move(defaultTex);
}

void InstancingAndCullingApp::BuildRootSignature()
{
	// 在HLSL里,有带7个元素的纹理数组,
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0, 0);
	// 根参数各个成员初始化, 按变更频率从高到低排序
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsShaderResourceView(0, 1);// 管线上绑定的0号是 实例的结构化buffer--gInstanceData : register(t0, space1)
	slotRootParameter[1].InitAsShaderResourceView(1, 1);// 管线上绑定的1号是 材质的结构化buffer--gMaterialData : register(t1, space1)
	slotRootParameter[2].InitAsConstantBufferView(0);	// 管线上绑定的2号是 cbPass : b(0)
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);// 管线上绑定的3号是持有7个元素的纹理数组

	// 拿采样器集合
	auto staticSamplers = GetStaticSamplers();

	// root signature关联根参数
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// root signature结合Blob内存块创建出需要的场景用根签名
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	// 最终创建出根签名
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void InstancingAndCullingApp::BuildDescriptorHeaps()
{
	/// 填充SRV VIEW HEAP描述,并创建出持有7个视图的SRV HEAP,用来存7张纹理的纹理数组
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 7;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	/// 暂存SRV HEAP里的视图句柄,负责偏移用
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	/// 暂存所有纹理;从全局纹理表李取出所有的纹理,均是一份D3D12Resource
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto crateTex = mTextures["crateTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto grassTex = mTextures["grassTex"]->Resource;
	auto defaultTex = mTextures["defaultTex"]->Resource;

	// 填充SRV视图的 描述并创建出bricksTex 纹理的SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;   // 采样时,返回的坐标上的向量
	srvDesc.Format = bricksTex->GetDesc().Format;								  // 格式先指定为自身纹理
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;						  // 资源维数
	srvDesc.Texture2D.MostDetailedMip = 0;										  // 最大的mipmap层级 0~MipLevels-1
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;				  // 若设为-1,则表示从MostDetailedMip到最后1个mipmap之间的所有mipmap
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = crateTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = iceTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// 偏移到下一个句柄, 创建出新纹理的SRV
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = defaultTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = defaultTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(defaultTex.Get(), &srvDesc, hDescriptor);
}

void InstancingAndCullingApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void InstancingAndCullingApp::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");//把这个文件用作输入流
	if (!fin) {
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;// 用以保存骷髅头顶点数
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;
	// 使用包围盒来计算骷髅头模型mesh的盒子
	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);// 初始化Vmin
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);// 初始化Vmax
	XMVECTOR vMin = XMLoadFloat3(&vMinf3);// 转成向量型
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);// 转成向量性

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;// 读模型各顶点分量
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;// 读模型各法线分量
		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);// 暂存单个顶点位置

		// 把点投射到单位球上 并生成 球面坐标 spherePos
		XMFLOAT3 spherePos;
		XMStoreFloat3(&spherePos, XMVector3Normalize(P));
		// 计算出角度theta
		float theta = atan2f(spherePos.z, spherePos.x);
		// 把角度theta限制在 [0, 2pi].
		if (theta < 0.0f)
			theta += XM_2PI;
		// 用球面坐标spherePos计算出角度phi
		float phi = acosf(spherePos.y);

		float u = theta / (2.0f * XM_PI);
		float v = phi / XM_PI;

		vertices[i].TexC = { u, v };// 算出UV

		vMin = XMVectorMin(vMin, P);// 取Vmin和P各分量的最小值
		vMax = XMVectorMax(vMax, P);// 取Vmax和P各分量的最大值
	}

	// 声明1个包围盒并计算出其c 和 e
	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center,  0.5f * (vMin + vMax));
	XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);// 三角面
	for (UINT i = 0; i < tcount; ++i) {
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	
	/// 下面就是把数据源拷贝到CPU端和GPU端构建顶点缓存和索引缓存,给geo做值
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";
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
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	submesh.Bounds = bounds;// 新增的包围盒子

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void InstancingAndCullingApp::BuildPSOs()
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
}

void InstancingAndCullingApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, mInstanceCount, (UINT)mMaterials.size()));
	}
}

void InstancingAndCullingApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;

	auto crate0 = std::make_unique<Material>();
	crate0->Name = "checkboard0";
	crate0->MatCBIndex = 3;
	crate0->DiffuseSrvHeapIndex = 3;
	crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	crate0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	crate0->Roughness = 0.2f;

	auto ice0 = std::make_unique<Material>();
	ice0->Name = "ice0";
	ice0->MatCBIndex = 4;
	ice0->DiffuseSrvHeapIndex = 4;
	ice0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ice0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	ice0->Roughness = 0.0f;

	auto grass0 = std::make_unique<Material>();
	grass0->Name = "grass0";
	grass0->MatCBIndex = 5;
	grass0->DiffuseSrvHeapIndex = 5;
	grass0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	grass0->Roughness = 0.2f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 6;
	skullMat->DiffuseSrvHeapIndex = 6;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness = 0.5f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["crate0"] = std::move(crate0);
	mMaterials["ice0"] = std::move(ice0);
	mMaterials["grass0"] = std::move(grass0);
	mMaterials["skullMat"] = std::move(skullMat);
}

void InstancingAndCullingApp::BuildRenderItems()
{
	auto skullRitem = std::make_unique<RenderItem>();// 构建一个骷髅头渲染项
	skullRitem->World = MathHelper::Identity4x4();// 设定骷髅头的世界矩阵为单位矩阵
	skullRitem->TexTransform = MathHelper::Identity4x4();// 设定 纹理变换矩阵也为默认单位矩阵
	skullRitem->ObjCBIndex = 0;// 设定 骷髅头这一种种类的物体 物体索引为 0号(1种控制125个实例)
	skullRitem->Mat = mMaterials["tile0"].get();// 设定 骷髅头的材质
	skullRitem->Geo = mGeometries["skullGeo"].get();// 设定 影响波浪的几何体形状的管理员Geo是
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 设定DrawIndexedInstanced渲染参数 实例数量为0
	skullRitem->InstanceCount = 0;
	// 设定DrawIndexedInstanced三项渲染参数为 管理员里的数据
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	// 设定骷髅头渲染项的包围体是 submeshgeometry里的 Bounds
	skullRitem->Bounds = skullRitem->Geo->DrawArgs["skull"].Bounds;

	// 专门给骷髅头使用实例化技术; 构造125个实例化个体,也是125次实例化
	const int n = 5;
	mInstanceCount = n * n * n;
	skullRitem->Instances.resize(mInstanceCount);


	float width = 200.0f;
	float height = 200.0f;
	float depth = 200.0f;

	float x = -0.5f * width;
	float y = -0.5f * height;
	float z = -0.5f * depth;
	float dx = width / (n - 1);
	float dy = height / (n - 1);
	float dz = depth / (n - 1);
	for (int k = 0; k < n; ++k) {
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < n; ++j) {
				
				int index = k * n * n + i * n + j;// 125个实例中的第几个骷髅头mesh?

				// Position instanced along a 3D grid.
				// 设置实例集合里每个实例(即单个骷髅头) 设置各自的 3D坐标
				skullRitem->Instances[index].World = XMFLOAT4X4(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					x + j * dx, y + i * dy, z + k * dz, 1.0f);
				// 设置实例集合里每个实例(即单个骷髅头) 设置各自的 纹理变换
				XMStoreFloat4x4(&skullRitem->Instances[index].TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
				// // 设置实例集合里每个实例(即单个骷髅头) 设置各自的 材质索引
				skullRitem->Instances[index].MaterialIndex = index % mMaterials.size();
			}
		}
	}

	// 全局渲染项数组仅注册 骷髅头这1个渲染项
	mAllRitems.push_back(std::move(skullRitem));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void InstancingAndCullingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// 对于每次的渲染项, 实例结构buffer可以绕过堆,直接在管线上设置为root desciptor
		auto instanceBuffer = mCurrFrameResource->InstanceBuffer->Resource();// 当前帧里的实例结构体buffer
		mCommandList->SetGraphicsRootShaderResourceView(0, instanceBuffer->GetGPUVirtualAddress());// 实例结构化buffer在管线上绑定到0号

		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount/*此处不再是原先的1,而是设计为实例的数量*/, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> InstancingAndCullingApp::GetStaticSamplers()
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

