//***************************************************************************************
// 大多离线AO的计算方式。具体策略是：随机投射出一些光线，使他们穿过以顶点P为中心的半球，
// 并检测这些光线与周围“障碍物”网格的相交情况，如果总共投射了N条光线，而其中的h条与网格相交，则顶点P所对应的遮蔽率 h/n
// 将光线通过每个三角网格的重心点，构建出通过重心点的半圆，然后计算出所有三角形共用顶点的平均occlusion。如图所示，所标数字为光线通过三角网格后，网格上3个顶点的遮蔽率，最终顶点的occlusion（遮蔽率）为共用顶点遮蔽率的平均值
// SSAO简单来说，就是使用屏幕空间的法线和深度值来计算每个屏幕像素的环境光遮蔽率
// 根据屏幕空间的法线和深度，可以计算得到屏幕空间的SSAO数据，并将其生成纹理图，这张纹理称作SSAO图
// 点q是以点p为中心的半球内的随机一点，点r则是从观察点到q这一路径上的最近可视点。如果Pz-Rz足够小，
// 且r-p与n之间的夹角小于90°，那么点r将计入点p的遮蔽值。同样可以使用通过点p的随机向量作为光线计算遮蔽率，最终求取他们平均值作为p点的遮蔽率。
// 使用“双边模糊”来处理SSAO图，双边模糊和高斯模糊很像，唯一的区别是在像素边缘是不做模糊处理的，这样可以保证AO的硬边
//***************************************************************************************
// SSAO做法是每帧把场景里view space里的法线绘制到一个full screen render target里,并把场景里的depth绘制到普通的深度模板缓存里
// 接着,仅用上述的法线渲染目标和深度模板作为输入,求解估算每个像素的AO数据
// 只要拿取了存有每个像素AO数据的这块纹理,就可以以纹理里的SSAO信息为每个像素调整ambient属性,再像往常一样把"处理后的场景"绘制到RT里

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "ShadowMap.h"
#include "Ssao.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

/* 暂定3个帧资源*/
const int gNumFrameResources = 3;

/// SSAO工程要用到的渲染项
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	XMFLOAT4X4 World = MathHelper::Identity4x4();// 世界矩阵(包含物体的位置、朝向、缩放)
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();//每个渲染项都持有 单物体独有的纹理变换矩阵
	int NumFramesDirty = gNumFrameResources;// 每个渲染项都持有 帧Dirty标记，表示对象数据已经改变，需要更新常量缓冲区;设置NumFramesDirty = gNumFrameResources，以便每个帧资源获得更新
	UINT ObjCBIndex = -1;// 每个渲染项都持有 单物体位于GPU的CB索引，也同时对应于这个渲染项的ObjectCB。
	Material* Mat = nullptr;// 每个渲染项都持有 1个材质
	MeshGeometry* Geo = nullptr;// 每个渲染项都持有 1个几何管理员
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;// 每个渲染项都持有图元类型,默认为三角形列表

	/* 每个渲染项都持有绘制三参数(分别是索引数、起始索引、基准地址)*/
	UINT IndexCount = 0;        // 绘制三参数之索引数
	UINT StartIndexLocation = 0;// 绘制三参数之起始索引
	int BaseVertexLocation = 0; // 绘制三参数之基准地址(顶点)
};

/// 渲染层级枚举
enum class RenderLayer : int
{
	Opaque = 0,// 非透明物体 层级
	Debug,// 保存在阴影上的的采样ShadowMap 层级
	Sky,// CUBEMAP天空球 层级
	Count
};

/// <summary>
/// 应用程序类SsaoApp
/// </summary>
class SsaoApp : public D3DApp
{
public:
	SsaoApp(HINSTANCE hInstance);
	SsaoApp(const SsaoApp& rhs) = delete;
	SsaoApp& operator=(const SsaoApp& rhs) = delete;
	~SsaoApp();

	virtual bool Initialize()override;

private:
	virtual void CreateRtvAndDsvDescriptorHeaps()override;// 重写框架方法:此虚函数负责创建渲染程序所需的RTV和DSV视图堆
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateSsaoCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildSsaoRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawSceneToShadowMap();
	void DrawNormalsAndDepth();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index)const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index)const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index)const;

	/* 自定义7个采样器构成的数组*/
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;// 帧资源数组
	FrameResource* mCurrFrameResource = nullptr;				// 当前帧资源
	int mCurrFrameResourceIndex = 0;							// 当前帧资源序数

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;		// 场景        的专用根签名
	ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;   // SSAO这种PASS的专用根签名

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;  // SRV堆,本项目中持有18个句柄

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries; // 全局几何体表
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;		// 全局材质表		
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;		// 全局贴图表
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;					// 全局Shader表
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;			// 全局管线表

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;							// 输入布局

	std::vector<std::unique_ptr<RenderItem>> mAllRitems;						// 全局渲染项

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];				// 由PSO来划分的渲染项层级数组

	/* 用以保存每个物体在SRV堆中的索引*/
	UINT mSkyTexHeapIndex = 0;			
	UINT mShadowMapHeapIndex = 0;
	UINT mSsaoHeapIndexStart = 0;
	UINT mSsaoAmbientMapIndex = 0;
	UINT mNullCubeSrvIndex = 0;
	UINT mNullTexSrvIndex1 = 0;
	UINT mNullTexSrvIndex2 = 0;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrvHandleForShadowmap;// 空SRV 在SRV堆中所处位置的句柄,注意,它的位置在"6张2D纹理","天空球","ShadowMap","SSAO","SSAO Ambient"之后

	/* 有2个PASSCB*/
	PassConstants mMainPassCB;  // PASS CB的0号:主场景PASS
	PassConstants mShadowPassCB;// PASS CB的1号:阴影图PASS

	Camera mCamera;// 摄像机

	std::unique_ptr<ShadowMap> mShadowMap;// ShadowMap资源的唯一指针
	std::unique_ptr<Ssao> mSsao;// SSAO资源的唯一指针

	DirectX::BoundingSphere mSceneBounds;// 包围球, UpdateShadowTransform函数里会用到,用包围球的中心作为观察目标

	float mLightNearZ = 0.0f;								// 以灯光为基准的近平面值,非相机
	float mLightFarZ = 0.0f;								// 以灯光为基准的远平面值,非相机
	XMFLOAT3 mLightPosW;									// 用来保存光源坐标,在具体调用处视为眼睛位置
	XMFLOAT4X4 mLightView = MathHelper::Identity4x4();		// 变换至灯光空间的矩阵
	XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();		// 从灯光空间转NDC空间 的矩阵
	XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();// 最终的灯光空间转纹理空间 的矩阵,即S = lightView * lightProj * T

	float mLightRotationAngle = 0.0f;						// 保存某些光的旋转角度,详见Update()
	XMFLOAT3 mBaseLightDirections[3] = {					// 有3个太阳平行光的位置,字段里有默认值
		XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	XMFLOAT3 mRotatedLightDirections[3];					// 光源数组里的3盏灯的朝向,在Update()里会随时间持续变化

	POINT mLastMousePos;// 鼠标最后位置
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// 给调试版本开启运行时内存监测,监督内存泄漏
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try {
		SsaoApp theApp(hInstance);// 句柄填到本SSAO项目里
		// 如若SSAO工程没有执行初始化就直接返回
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();// Run接口负责, 如果没有消息传来,就持续地处理游戏逻辑:计算FPS和单帧毫秒长; 调用子类重写的纯虚函数Update; 调用子类重写的纯虚函数Draw
	}																		  
	catch (DxException& e) {												  
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

/// 程序构造器: 构造的时候,设定一下场景包围盒
SsaoApp::SsaoApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	// 手动估计场景边界球，因为我们知道场景是如何构建的。网格是“最宽的物体”，宽度为20，深度为30.0f，以世界空间原点为中心。通常情况下，需要遍历每个世界空间顶点位置并计算边界球
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);
}

/// 程序析构:强令CPU等待GPU
SsaoApp::~SsaoApp()
{
	// 强令CPU等待GPU
	if (md3dDevice != nullptr)
		D3DApp::FlushCommandQueue();
}

/// 场景初始化
bool SsaoApp::Initialize()
{
	// 先检查框架基类初始化
	if (!D3DApp::Initialize())
		return false;
	// 利用分配器刷新命令列表 以准备初始化命令
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// 场景初始化的时候 把摄像机放在指定位置
	mCamera.SetPosition(0.0f, 2.0f, -15.0f);
	// 场景初始化的时候 就构建出一张阴影图资源unique_ptr
	mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 2048, 2048);
	// 场景初始化的时候 就构建出一个SSAO资源unique_ptr
	mSsao = std::make_unique<Ssao>(md3dDevice.Get(), mCommandList.Get(), mClientWidth, mClientHeight);

	LoadTextures();/// 创建出所有的2D纹理 并 在全局纹理表里注册

	BuildRootSignature();/// 构建 场景根签名 (包含ObjectCB,PassCB,MatSB,2D纹理,CubeMap,注意各自的槽位),详见common.hlsl
	BuildSsaoRootSignature();/// 构建 SSAO的根签名,详见Ssao.hlsl
	BuildDescriptorHeaps();/// 1.创建出持有18个句柄的堆,并偏移句柄; 依次创建出2D纹理(含法线纹理)、天空球、
						   /// 2.依次创建出ShadowMap、SSAO、SSAOAmbientmap的SRV 期间也顺带保留了它们各自在堆中的序数
						   /// 3.针对shadowmap和ssao这两种资源,还要额外的创建出DSV和RTV,详见最后两个接口

	BuildShadersAndInputLayout();/// 全局shader表里注册各种shader并填充顶点输入布局

	BuildShapeGeometry();/// MeshGeometry类型的geo管理员各属性做值,管理场景类的一些场景几何体
	BuildSkullGeometry();/// MeshGeometry类型的geo管理员各属性做值,管理场景类的骷髅头

	BuildMaterials();/// 构建出所有材质(注意材质序数和其使用的2D纹理位于堆的位置) 并 注册到全局材质表
	BuildRenderItems();/// 构建所有物体的渲染项(含天空球、面片、盒子、骷髅头、地板、两侧柱子)
	BuildFrameResources();/// 构建3个帧资源并存到数组里 每帧里构造出出2个PassCB, 1个SSAOCB, 渲染项个数的ObjectCB, 5个结构体材质

	BuildPSOs();/// 构建各种的自定义的管线,详见函数内部

	// 从全局管线表里 指定SSAO专用流水线, 双边模糊专用流水线
	mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());

	// 执行完上述各初始化步骤后, 先关闭命令列表记录步骤 再 构建命令列表数组并在队列里执行命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 强制CPU等待GPU
	FlushCommandQueue();

	return true;
}

/// 重写框架方法:此虚函数负责创建渲染程序所需的RTV(5个视图)和DSV视图堆(2个视图)
void SsaoApp::CreateRtvAndDsvDescriptorHeaps()
{
	// 创造出持有5个视图的 RTV堆
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 1 + 2;// 注意这里视图数量发生了变化,多出的3个其中1个给屏幕法线map, 另外2个给2张ambient map
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// 创建出带2个视图的 DSV堆
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + 1;// 注意这里视图数量发生了变化,多出来的那1个给 shadow map的DSV用
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void SsaoApp::OnResize()
{
	D3DApp::OnResize();
	
	// 由于窗口发生尺寸改变, 故需要重建透视投影矩阵
	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	// 窗口尺寸改变的时候检测SSAO资源存在性,不存在就执行OnResize ssao资源并重建视图
	if (mSsao != nullptr) {
		mSsao->OnResize(mClientWidth, mClientHeight);

		// 重建SSAO的视图资源,此处是重建法线图SRV、深度的SRV、随机向量图SRV、环境光图0号1号SRV、法线图RTV、2张环境光图的RTV
		mSsao->RebuildDescriptors(D3DApp::mDepthStencilBuffer.Get());
	}
}

/// 每帧都更新的逻辑
void SsaoApp::Update(const GameTimer& gt)
{
	/* 按键事件 WASD移动相机 并 每帧重建观察矩阵*/
	OnKeyboardInput(gt);

	/* 每帧刷新 当前帧用的 帧资源和帧资源索引*/
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;// 取模
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();// 从帧资源集里取出当前帧资源

	/* 强制令CPU等待至GPU处理完当前帧资源里围栏前所有命令*/
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	// 光源和阴影的动画

	mLightRotationAngle += 0.5f * gt.DeltaTime();// 这是一个变动值; 暂存 每帧持续变化的递增的改变主光源的角度(float)

	/* 得到随时间变化的 平行光朝向角度*/
	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);// 用变动值制造一个 变化的yaw旋转矩阵
	// 注意!!!本项目暂定全局光源数组里有3盏灯
	for (int i = 0; i < 3; ++i) {
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);// 先加载出那三个光 每个光的位置
		lightDir = XMVector3TransformNormal(lightDir, R);// 然后 用Yaw旋转矩阵 去变换每个光的位置(3D法线) 
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);// 最后 把变化了的光的位置存到 光源数组每盏灯的角度里;;mRotatedLightDirections这个字段还会在主PASS力用到
	}

	AnimateMaterials(gt);/// 暂无逻辑

	UpdateObjectCBs(gt);/// 做值, 然后更新本帧每个渲染项里(即每个物体)的 ObjectCB
	UpdateMaterialBuffer(gt);/// 遍历全局材质表来 更新本帧内每个 结构化材质

	UpdateShadowTransform(gt);/// 构建灯光观察矩阵、灯光转NDC矩阵、 灯光转TexCoord矩阵到字段

	UpdateMainPassCB(gt);/// 更新主PASSCB,注意,主PASS是0号,有2个PASS,另外一个是ShadowPass
						 /// 主PASS里有ShadowTransform, 要将其传入MainPassCB，而不是ShadowMapPassCB，
						 /// 因为阴影是在绘制主场景时使用阴影图计算的，而阴影图本体绘制才是使用的ShadowMapPassCB

	UpdateShadowPassCB(gt);/// 构造阴影图自身PASS, ShadowPassCB数据传入GPU流水线
	
	UpdateSsaoCB(gt);/// 以1个SsaoConstants实例为其做值, 更新本帧的SSAOCB 特效
}

/// 每帧的绘制
void SsaoApp::Draw(const GameTimer& gt)
{
	/* 拿取当前帧资源里的分配器并重置*/
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	/* 复用命令列表,PSO刷新为非透明管线*/
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	/* 管线上绑定 SRV HEAP数组*/
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	
	/* 管线上绑定 "场景"的根签名*/
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());/// 第一次仍设置为 "场景根签名"

	/// ShadowMap Pass相关资源绑定(材质、空SRV、纹理):
	///
	/* ShadowMap Pass: 在管线上绑定a root descriptor(可以绕过堆,并设置为1个根描述符): "本帧的结构化材质Resource"(即场景里要用到的所有材质), 其设定为2号*/
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();// 本帧 结构化材质
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	/* ShadowMap Pass: 在管线上绑定DescriptorTable: "供Shadowmap Pass渲染过程用的空着色器视图", 其设定为3号*/
	mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrvHandleForShadowmap);

	/* ShadowMap Pass: 在管线上绑定DescriptorTable: "这个场景中使用的所有纹理,注意，我们只需要指定table中的第一个描述符,根签名知道有多少个descriptor在table里",其设定为4号*/
	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	/* 绘制场景里 深度 到一张关联ShadowMap的纹理内*/
	DrawSceneToShadowMap();

	/* 绘制场景里 各物体位于观察空间的法线和深度 渲染到额外的一个屏幕大小的纹理,关联SSAO,然后将该纹理作为输入来估算每个像素点的环境光遮蔽程度*/
	DrawNormalsAndDepth();

	/// 计算SSAO
	///
	mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());/// 切换为SSAO专用根签名
	mSsao->ComputeSsao(mCommandList.Get(), mCurrFrameResource, 3);// 在管线上针对SSAO这种特效执行一些常见绑定设置和绘制出带6个角点的quad, 并对环境光图执行双边模糊

	/// 主Pass.(0号是物体,1号是PassCB,2号是结构化材质buffer,3号是天空球,4号是所有场景纹理)
	///
	// 重新设定状态无论根签名何时变化
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());/// 根签名保持不变,认为场景根签名

	/* 主PASS: 在管线上绑定a root descriptor(可以绕过堆,并设置为1个根描述符): "本帧的结构化材质Resource"(即场景里要用到的所有材质), 其设定为2号*/
	matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	/* 重设视口和裁剪矩形*/
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	/* 资源转换: 后台缓存从呈现态切换为待渲染目标态*/
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
	
	/// 在DrawNormalsAndDepth方法里已经写入了深度缓存,因此无须再第二次清除深度
	// 在主PASS里使用淡蓝色清除 RTV
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	// 绑定主PASS的渲染目标视图 是 后台缓存
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
	// 绑定主PASS里,场景中使用的所有纹理。观察得出结论,仅需要指定表中的第一个描述符, 而根签名知道表中需要多少描述符, 设定为4号
	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	// 取出并绑定主PASS里当前场景的PassCB,设定为1号
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// 绑定天空球Cube map, 设定为3号
	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());// 先暂存一下句柄并偏移至天空球
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	// 切换为"非透明管线",并绘制出所有层级为非透明的物体
	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	// 切换为"阴影到面片管线", 并绘制出所有层级为shadow map的层级
	mCommandList->SetPipelineState(mPSOs["debug"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);
	// 切换为"天空球管线", 并绘制出所有层级为"天空球"的层级
	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	// 后台缓存切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// 结束命令记录并组建命令数组打到队列里真正执行命令数组
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 交换前后台缓存并呈现
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;// 后台缓存序数+1并对交换链个数取模

	// 更新每帧里的围栏 为 基类自增后的围栏, 把命令标记到围栏点
	mCurrFrameResource->Fence = ++D3DApp::mCurrentFence;
	// 向命令队列添加一条指令专门用来设置一个新的围栏点
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

/// 鼠标按下事件:更新最后坐标以及捕捉主窗口
void SsaoApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

/// 鼠标松开事件:释放主窗口捕捉
void SsaoApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

/// 光标移动事件: 让摄像机视角随光标坐标移动
void SsaoApp::OnMouseMove(WPARAM btnState, int x, int y)
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

/// 按键事件 WASD移动相机 并 每帧重建观察矩阵
void SsaoApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();// 帧时间差
	/* 按帧时间差WASD移动*/
	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f * dt);
	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);
	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);
	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);
	
	/* 每帧都需要 重建 观察矩阵*/
	mCamera.UpdateViewMatrix();
}

/// 暂无逻辑,变化材质的逻辑
void SsaoApp::AnimateMaterials(const GameTimer& gt)
{

}

/// 自定义1个ObjectConstants型实例给它做值, 然后更新本帧每个渲染项里(即每个物体)的 ObjectCB
void SsaoApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();// 本帧的ObjectCB,注意,它是一个UploaderBuffer
	/* 遍历所有的渲染项,即遍历所有的物体*/
	for (auto& e : mAllRitems) {
		// 只要渲染项里的帧脏标记(默认值为3)命中,就执行逻辑
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);// 暂存渲染项里的世界变换矩阵
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);// 暂存渲染项里的纹理变换矩阵
			/* 做值,做数据源, ObjectConstants型实例*/
			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));// 用渲染项里的世界变换矩阵 填充 ObjectConstants型实例
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));// 用渲染项里的纹理变换矩阵 填充 ObjectConstants型实例
			objConstants.MaterialIndex = e->Mat->MatCBIndex;// 用渲染项里的材质索引 去填充 ObjectConstants型实例
			/* 拷贝数据源"ObjectConstants型实例" 至本帧ObjectCB里 第e->ObjCBIndex个缓存区*/
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// 渲染项里帧标记自减, 对下一个FrameResource执行更新
			e->NumFramesDirty--;
		}
	}
}

/// 遍历全局材质表来 更新本帧内每个 结构化材质
void SsaoApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();// 本帧结构化材质,注意,它是一个UploaderBuffer
	/* 遍历全局材质表里的每个材质*/
	for (auto& e : mMaterials) {
		Material* mat = e.second.get();// 拿到全局材质表里的每个材质
		if (mat->NumFramesDirty > 0) {// 只要材质里的帧脏标记(默认值为3)命中,就执行做值逻辑
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);// 暂存材质里的MatTransform
			/* 做值,做1个结构化材质matData,作为数据源*/
			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;
			/* 用matData更新本帧结构化材质, 第MatCBIndex个材质*/
			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// 材质里帧标记自减, 对下一个FrameResource执行更新
			mat->NumFramesDirty--;
		}
	}
}

/// 构建灯光观察矩阵、灯光转NDC矩阵、 灯光转TexCoord矩阵到字段
void SsaoApp::UpdateShadowTransform(const GameTimer& gt)
{
	/*
	先计算整个场景的包围球，
	1.先构建出LightToProjection矩阵（灯光空间转NDC空间, LightSapce(View)->NDC Space
	2.接着算出NDC转纹理空间矩阵                         NDC Space->TexCoord Space，
	3.将这2个矩阵叠加相乘得到Light2Texcoord矩阵（灯光转纹理），
	因为没有齐次除法，所以NDC后可以直接乘以纹理矩阵，得到L2T矩阵。
	*/


	// 仅有主光源才投射阴影
	XMVECTOR lightDir = XMLoadFloat3(&mRotatedLightDirections[0]);	// 第一个平行光的光向量
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;		// 光源位置,调用处视为眼睛位置
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);		// 场景包围球球心,调用处视为观察目标
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);			// 世界向上分量
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);// 构建灯光左手观察矩阵

	XMStoreFloat3(&mLightPosW, lightPos);// 暂存光源坐标

	/* 先将包围球球心变换到光源空间, XMVector3TransformCoord这个接口负责把向量的w分量变为1*/
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));// 利用观察矩阵把这个包围球球心位置变换至观察空间

	/* 位于光源空间中包围场景的正交投影视景体*/
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;
	mLightNearZ = n; // 暂存近裁剪面距离
	mLightFarZ = f;	 // 暂存远裁剪面距离
	/* !!!//构建LightToProject矩阵（从灯光空间转NDC空间）*/
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);/// 先变换至NDC

	/* 构建NDCToTexture矩阵（NDC空间转纹理空间）
	 * 从[-1, 1]转到[0, 1]
	 */
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);/// 再变换至TexCoord
	
	/// 所有的都叠加在一起, 最终构建LightToTexture（灯光空间转纹理空间）
	XMMATRIX S = lightView * lightProj * T;// LightToTexture（灯光空间转纹理空间）

	/* 注册 灯光观察矩阵、灯光转NDC矩阵、 灯光转TexCoord矩阵 到字段里*/
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}

/// 更新主PASSCB
void SsaoApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();// 加载相机的观察矩阵
	XMMATRIX proj = mCamera.GetProj();// 加载相机的透视投影矩阵

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);					 // 暂存相机 观察投影矩阵
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);// 暂存相机 逆观察矩阵
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);// 暂存相机 逆投影矩阵
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);// 暂存相机 逆观察投影矩阵

	/* 构建NDCToTexture矩阵（NDC空间转纹理空间）,T这个矩阵它是后面viewProjTex的组成部分,从[-1, 1]转到[0, 1] */
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);// 暂存 观察投影矩阵 * NDCToTexture
	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);// 暂存 最终的灯光空间转纹理空间 的矩阵,即S = lightView * lightProj * T

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	
	/* 将ShadowTransform传入流水线
	 * 注意我们要将其传入MainPassCB，而不是ShadowMapPassCB，
	 * 因为阴影是在绘制主场景时使用阴影图计算的，而阴影图本体绘制才是使用的ShadowMapPassCB
	 */
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));// 更新主PASS的阴影图ShadowTransform,即灯光转纹理矩阵
	
	mMainPassCB.EyePosW = mCamera.GetPosition3f();// 设置主ASS眼睛位置是摄像机POS
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);// 设置阴影图要用的RenderTargetSize是后台的宽高
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;  // 设置主PASS的近裁剪面
	mMainPassCB.FarZ = 1000.0f;// 设置主PASS的远裁剪面
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.5f, 0.5f, 0.5f, 1.0f };// 设置主PASS的环境光颜色
	/* 设置主PASS里光源数组里3盏灯的朝向和颜色*/
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.4f, 0.4f, 0.5f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.1f, 0.1f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

	/* 更新主PASS,注意这里是0号; 因为主PASS是0号,ShdaowPass是1号,类里设置了共2个PassConstants实例; 将ShadowMap的PassCB存在1号索引（仅在Main之后)*/
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

/// 构造阴影图自身PASS, ShadowPassCB数据传入GPU流水线
void SsaoApp::UpdateShadowPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mLightView);// 这里不再是相机的View, 而是 变换至灯光空间的矩阵
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);// 这里不再是相机的Proj, 而是 从灯光空间转NDC空间的矩阵

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	/* 拿到阴影图的宽和高*/
	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;// 设置ShadowPass的眼睛位置 是 以光源为视角看过去的光源坐标
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);// 设置ShadowPass的渲染尺寸不再是后台宽高,而是 阴影纹理的宽高
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;// 设置阴影PASS的近平面为 以灯光为基准的近裁剪面,非相机的
	mShadowPassCB.FarZ = mLightFarZ;  // 设置阴影PASS的远平面为 以灯光为基准的远裁剪面,非相机的
	
	/* 将ShadowMap的PassCB存在1号索引（仅在Main之后, 举个例子,假如这里还有天空球,那么便设为7号）*/
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1/*注意有2个PASS,阴影PASS是1号,此前还有一个主PASS*/, mShadowPassCB);
}

/// 以1个SsaoConstants实例为其做值, 更新本帧的SSAOCB 特效
void SsaoApp::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB;// 声明数据源,一个ssaoCB

	XMMATRIX P = mCamera.GetProj();// 暂存相机投影矩阵

	/* 构建NDCToTexture矩阵（NDC空间转纹理空间）,T这个矩阵它是后面viewProjTex的组成部分,从[-1, 1]转到[0, 1] */
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	/* 给ssaoCB各属性做值*/
	ssaoCB.Proj = mMainPassCB.Proj;		 // 继承并复用 主PASS的 Proj
	ssaoCB.InvProj = mMainPassCB.InvProj;// 继承并复用 主PASS的 InvProj
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));// 投影Q点用的"投影纹理变换矩阵"ProjTex 由 P叠加T转置而来

	/* ssao资源里获取14个随机向量*/
	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	/* 设置数据源的3个模糊权重*/
	auto blurWeights = mSsao->CalcGaussWeights(2.5f);// 使用ssao资源计算出一组模糊权重
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	// 以下均在观察空间.
	ssaoCB.OcclusionRadius = 0.5f;		// 设置SSAO特效的 遮蔽半径
	ssaoCB.OcclusionFadeStart = 0.2f;	// 设置SSAO特效的 遮蔽衰落起始
	ssaoCB.OcclusionFadeEnd = 1.0f;		// 设置SSAO特效的 遮蔽衰减结束
	ssaoCB.SurfaceEpsilon = 0.05f;		// 设置SSAO特效的 用户自定义的容错范围值

	/// 更新本帧SsaoCB(用做出来的值即实例ssaoCB)
	auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
	currSsaoCB->CopyData(0/*0号,目前只有1个SSAO特效*/, ssaoCB);
}

/// 创建出所有的2D纹理 并 在全局纹理表里注册
void SsaoApp::LoadTextures()
{
	/* 初始化所有纹理的名字和路径*/
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",
		"tileDiffuseMap",
		"tileNormalMap",
		"defaultDiffuseMap",
		"defaultNormalMap",
		"skyCubeMap"
	};
	std::vector<std::wstring> texFilenames =
	{
		L"../../Textures/bricks2.dds",
		L"../../Textures/bricks2_nmap.dds",
		L"../../Textures/tile.dds",
		L"../../Textures/tile_nmap.dds",
		L"../../Textures/white1x1.dds",
		L"../../Textures/default_nmap.dds",
		L"../../Textures/sunsetcube1024.dds"
	};
	/* 创建出每张2D纹理并在全局纹理表里注册*/
	for (int i = 0; i < (int)texNames.size(); ++i) {
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}

/// 构建 场景根签名 (包含ObjectCB,PassCB,MatSB,2D纹理,CubeMap,注意各自的槽位),详见common.hlsl
void SsaoApp::BuildRootSignature()
{
	// Range设置
	CD3DX12_DESCRIPTOR_RANGE texTable0;// t0, space0, Cubemap Texture
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3/*描述符数量（纹理数量)*/, 0/*寄存器槽号,参见common.hlsl*/, 0);
	CD3DX12_DESCRIPTOR_RANGE texTable1;// t3, space0, 2d Texture(10张构成数组), 绑定纹理的Range时，修改纹理SRV的数量。
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10/*描述符数量（纹理数量)*/, 3/*寄存器槽号,参见common.hlsl*/, 0);

	// 按使用频率从高到低排列
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsConstantBufferView(0);    // objCB绑定槽号为"b0"的寄存器,但是space在0号, 参见common.hlsl
	slotRootParameter[1].InitAsConstantBufferView(1);	 // passCB绑定槽号为"b1"的寄存器,参见common.hlsl
	slotRootParameter[2].InitAsShaderResourceView(0, 1/*register space号*/); // matSB--(t0, space0),也绑定槽号为"t0"的寄存器,但是space却在1号（和CubeMap公用一个SRV寄存器，但是不同Space,参见common.hlsl
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);// CubeMap纹理绑定槽号为t0的寄存器,但是space却在0号
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);// 10个元素的2D纹理数组,放在槽号为t3的寄存器

	// 构建持有6个元素的静态采样器数组
	auto staticSamplers = GetStaticSamplers();

	// 填充rootSigDesc
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 结合Blob内存块创建出需要的场景用根签名
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
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

/// 构建 SSAO的根签名,详见Ssao.hlsl
void SsaoApp::BuildSsaoRootSignature()
{
	/* Range设置*/
	CD3DX12_DESCRIPTOR_RANGE texTable0;// t0, space0 2张储存2D贴图的纹理
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2/*描述符数量（某种纹理数量)*/, 0/*寄存器槽号,参见Ssao.hlsl*/, 0);
	CD3DX12_DESCRIPTOR_RANGE texTable1;// t2, space0 1张储存随机向量的纹理
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1/*描述符数量（某种纹理数量)*/, 2/*寄存器槽号,参见Ssao.hlsl*/, 0);

	/* 按使用频率从高到低排序根参数*/
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsConstantBufferView(0);// b0,space0, cbSsao,这个CB里保存SSAO要用到的一堆属性,  详见Ssao.hlsl
	slotRootParameter[1].InitAsConstants(1, 1);		 // b1,space0, cbRootConstants,这个CB里保存的是水平模糊, 详见Ssao.hlsl
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);// t0, space0 2张储存2D贴图的纹理
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);;// t2, space0 1张储存随机向量的纹理

	/* 构建一些给SSAO那些法线图和深度图采样的静态采样器*/
	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW
	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW
	const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,
		0,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);
	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW
	std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
	{
		pointClamp, linearClamp, depthMapSam, linearWrap
	};

	// 填充rootSigDesc
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 结合Blob内存块创建出需要的 SSAO用根签名
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
		IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

/// 1.创建出持有18个句柄的堆,并偏移句柄; 依次创建出2D纹理(含法线纹理)、天空球、
/// 2.依次创建出ShadowMap、SSAO、SSAOAmbientmap的SRV 期间也顺带保留了它们各自在堆中的序数
/// 3.针对shadowmap和ssao这两种资源,还要额外的创建出DSV和RTV,详见最后两个接口
void SsaoApp::BuildDescriptorHeaps()
{
	/* 创建出持有18个句柄的 SRV堆*/
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 18;// 有18个句柄
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	/* 从SRV堆中取出首句柄, 以作偏移用*/
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	/* 提出全局贴图表里的一些贴图资源(这里有2D漫反射纹理、2D法线纹理), 构成贴图数组*/
	std::vector<ComPtr<ID3D12Resource>> tex2DList =
	{
		mTextures["bricksDiffuseMap"]->Resource,
		mTextures["bricksNormalMap"]->Resource,
		mTextures["tileDiffuseMap"]->Resource,
		mTextures["tileNormalMap"]->Resource,
		mTextures["defaultDiffuseMap"]->Resource,
		mTextures["defaultNormalMap"]->Resource
	};
	/* 提出天空球用的CubeMap纹理资源,暂存为1个D3D12Resource*/
	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

	/* 给每张 2D漫反射和法线纹理 创建SRV并偏移到下一个*/
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;// 类型是2D纹理
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	for (UINT i = 0; i < (UINT)tex2DList.size(); ++i) {
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);// 创建出单次迭代的 2D贴图的 SRV

		// 句柄偏移到下一个
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}
	/* 顺便也给 CUBEMAP贴图 创建SRV*/
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;// 类型切换为CubeMap
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;// 切换为天空球的CubeMap d3d12资源
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get()/*D3D12资源*/, &srvDesc, hDescriptor/*该资源对应的句柄*/);// 创建出CPU端的 天空球CubeMap的 SRV

	/* 更新各字段在堆中的排序索引*/
	mSkyTexHeapIndex = (UINT)tex2DList.size();     //::0~5是6张2D纹理,6号是天空球
	mShadowMapHeapIndex = mSkyTexHeapIndex + 1;    //::0~5是6张2D纹理,6号是天空球,7号:ShadowMap
	mSsaoHeapIndexStart = mShadowMapHeapIndex + 1; //::0~5是6张2D纹理,6号是天空球,7号:ShadowMap,8~10号:SSAO	
	mSsaoAmbientMapIndex = mSsaoHeapIndexStart + 3;//::0~5是6张2D纹理,6号是天空球,7号:ShadowMap,8~10号:SSAO,11~12号:SSAO Ambient
	mNullCubeSrvIndex = mSsaoHeapIndexStart + 5;   //::0~5是6张2D纹理,6号是天空球,7号:ShadowMap,8~10号:SSAO,11~12号:SSAO Ambient,13号:承接Cubemap纹理的SRV
	mNullTexSrvIndex1 = mNullCubeSrvIndex + 1;
	mNullTexSrvIndex2 = mNullTexSrvIndex1 + 1;

	/* 首先偏移空SRV句柄到 指定堆中序数(CPU端和GPU端) 并最终创建出其对应的SRV*/
	auto nullSrv = GetCpuSrv(mNullCubeSrvIndex);//CPU端空SRV在堆中序数
	mNullSrvHandleForShadowmap = GetGpuSrv(mNullCubeSrvIndex);//GPU端空SRV在堆中序数
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);// 创建出CPU端的空SRV的 SRV

	/* 空SRV再往后偏移1格,此时视作承接阴影图用*/
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;// 切换为2D纹理,存放阴影图
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);// 创建出CPU端 阴影图的 SRV

	/* 空SRV再往后偏移1格,此时视作承接SSAO*/
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);// 创建出CPU端 SSAO的 SRV

	/* 针对ShadowMap指针: 1.暂存外部SRV\DSV句柄,2.并给阴影图创建出 SRV(cpu端)和DSV(cpu端), 以便后续 阴影图的采样和渲染*/
	mShadowMap->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	/* 针对SSAO指针, 保存指定的5个描述符句柄的引用(分别是2张AmbientMap,1张法线图,1张深度图,1张随机向量图) 并利用这5个句柄创建出关联SSAO效果的各自的SRV 和 RTV*/
	mSsao->BuildDescriptors(
		mDepthStencilBuffer.Get(),
		GetCpuSrv(mSsaoHeapIndexStart),
		GetGpuSrv(mSsaoHeapIndexStart),
		GetRtv(SwapChainBufferCount),
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize);
}

/// 全局shader表里注册各种shader并填充顶点输入布局
void SsaoApp::BuildShadersAndInputLayout()
{
	/* 自定义1个阿尔法测试宏*/
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};
	// 标准shader--Default.hlsl
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	// 给阴影图用的shader--Shadows.hlsl
	mShaders["shadowVS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");// 这一张是在像素着色器里开启阿尔法测试
	// 渲染SSAO图到quad上的shader,注意,hlsl里采样的是SSAO MAP而不是shadowmap,且此时顶点着色器里的顶点已经处于齐次裁剪空间了!!!!!!!!
	mShaders["debugVS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");
	// 责把场景里各物体位于view space里的法向量渲染到与屏幕大小一致,格式一致的纹理内的 shader
	mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"Shaders\\DrawNormals.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"Shaders\\DrawNormals.hlsl", nullptr, "PS", "ps_5_1");
	// 布置好观察空间法线和场景深度后,禁用深度缓存,并在每个像素处调用SSAO的像素着色器绘制一个全屏四边形,该shader生成一个对应的环境光可及率数据
	mShaders["ssaoVS"] = d3dUtil::CompileShader(L"Shaders\\Ssao.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoPS"] = d3dUtil::CompileShader(L"Shaders\\Ssao.hlsl", nullptr, "PS", "ps_5_1");
	// 变远变模糊的SSAO(双边模糊) 让SSAO图的过渡更加平滑
	mShaders["ssaoBlurVS"] = d3dUtil::CompileShader(L"Shaders\\SsaoBlur.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoBlurPS"] = d3dUtil::CompileShader(L"Shaders\\SsaoBlur.hlsl", nullptr, "PS", "ps_5_1");
	// CubeMap用的天空球shader
	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	/* 顶点的输入布局*/
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

/// MeshGeometry类型的geo管理员各属性做值,管理场景类的一些场景几何体
void SsaoApp::BuildShapeGeometry()
{
	/* 用几何生成器结合各种算法,生成一些几何体mesh*/
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(-1.0f, -0.25f, 0.5f, 0.5f, 0.0f);// 此处影响生成的QUAD处于屏幕的位置

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// 缓存在全局VB/IB中的偏移,方便后续各个几何体的绘制三参数
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	/* 填充各个几何体的绘制三参数*/
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;
	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;
	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;

	/* 暂存全局顶点数*/
	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quad.Vertices.size();
	/* 开辟等值于顶点数的 "全局顶点数组"用以保存全局顶点缓存*/
	std::vector<Vertex> vertices(totalVertexCount);

	/* 给全局顶点数组里的各关联几何体的区间顶点段 做值*/
	UINT k = 0;// k用来给全局顶点划分区间段的
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)// 方块盒区间
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)// 地板栅格
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)// 圆柱上的球
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)// 圆柱
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}
	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)// 用以保存SSAO的右下角quad
	{
		vertices[k].Pos = quad.Vertices[i].Position;
		vertices[k].Normal = quad.Vertices[i].Normal;
		vertices[k].TexC = quad.Vertices[i].TexC;
		vertices[k].TangentU = quad.Vertices[i].TangentU;
	}

	/* 给全局索引数组里的各关联几何体的区间索引段 做值*/
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

	/* 暂存VB/IB值来源 的字节大小*/
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	/* 构建1个几何管理员geo指针管理各个subMesh*/
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";// geo命名
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);// 全局顶点数组拷贝至geo的CPU端VB
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);// 全局索引数组拷贝至geo的CPU端IB
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);// 全局顶点数组拷作为数据源借助geo里的上传堆流传至GPU端VB,创建出顶点缓存
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);// 全局索引数组拷作为数据源借助geo里的上传堆流传至GPU端IB,创建出索引缓存
	/* 接上一步,给geo填充其他属性*/
	geo->VertexByteStride = sizeof(Vertex);// 单顶点字节---用于构建VertexBufferView
	geo->VertexBufferByteSize = vbByteSize;// 顶点缓存字节---用于构建VertexBufferView
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;// 索引格式---用于构建IndexBufferView
	geo->IndexBufferByteSize = ibByteSize;//  索引缓存字节---用于构建IndexBufferView
	geo->DrawArgs["box"] = boxSubmesh;    // DrawArgs子选项设定为 各个物体的submesh
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quad"] = quadSubmesh;
	/* 全局几何体注册geo*/
	mGeometries[geo->Name] = std::move(geo);
}

/// MeshGeometry类型的geo管理员各属性做值,管理场景类的骷髅头
void SsaoApp::BuildSkullGeometry()
{
	/* 让fin读取这个文件 "Models/skull.txt" */
	std::ifstream fin("Models/skull.txt");
	if (!fin) {
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;// 用以保存骷髅头顶点数
	UINT tcount = 0;// 用以保存骷髅头三角形数
	std::string ignore;

	fin >> ignore >> vcount;//读取vertexCount并赋值
	fin >> ignore >> tcount;//读取triangleCount并赋值
	fin >> ignore >> ignore >> ignore >> ignore;//整行不读
	// 使用包围盒来计算骷髅头模型mesh的盒子
	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
	XMVECTOR vMin = XMLoadFloat3(&vMinf3);// 加载出向量
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);// 加载出向量
	//初始化顶点列表,全局顶点数组
	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;			// 读模型各顶点分量
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;// 读模型各法线分量
		vertices[i].TexC = { 0.0f, 0.0f };											// 由于使用法线贴图技术,所以这自定义纹理坐标{0,0}

		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);// 暂存本次迭代的 顶点位置属性
		XMVECTOR N = XMLoadFloat3(&vertices[i].Normal);// 暂存本次迭代的 顶点法线属性

		//生成一个切向量，使法线映射工作。没有申请纹理贴图到头骨，所以我们只需要任何切向量
		//通过数学计算得到原始插值顶点切线
		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);// 向上分量
		if (fabsf(XMVectorGetX(XMVector3Dot(N, up))) < 1.0f - 0.001f) {
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}
		else {
			up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, up));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}

		vMin = XMVectorMin(vMin, P);// 取Vmin和P各分量的最小值
		vMax = XMVectorMax(vMax, P);// 取Vmax和P各分量的最大值
	}
	// 声明1个包围盒并计算出其c 和 e
	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax)); // 取Vmin和P各分量的最小值
	XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));// 取Vmax和P各分量的最大值

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);// 三角面,所以全局索引数组 * 3
	for (UINT i = 0; i < tcount; ++i) {
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();// 关闭输入流


	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	/* 下面就是把数据源拷贝到CPU端和GPU端构建顶点缓存和索引缓存,给geo做值*/
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";// 管理骷髅头的geo
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
	submesh.Bounds = bounds;// 包围体盒子
	geo->DrawArgs["skull"] = submesh;
	// 骷髅头的geo 注册进全局几何体
	mGeometries[geo->Name] = std::move(geo);
}

/// 构建各种的自定义的管线,详见函数内部
void SsaoApp::BuildPSOs()
{
	/* 有1条基管线*/
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;
	/* 基PSO 使用Default.hlsl的VS、PS*/
	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));// 先清空一下管线
	basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };//输入布局为mInputLayout
	basePsoDesc.pRootSignature = mRootSignature.Get();
	basePsoDesc.VS =															// 使用标准 顶点shader
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	basePsoDesc.PS =															// 使用非透明 像素shader
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);		// 指定光栅化状态
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);					// 指定混合状态
	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);	// 配置深度模板状态
	basePsoDesc.SampleMask = UINT_MAX;											// 设置每个采样点的采集情况
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;	// 图元拓扑类型
	basePsoDesc.NumRenderTargets = 1;											// 只有1个RT
	basePsoDesc.RTVFormats[0] = mBackBufferFormat;								// 渲染目标格式
	basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;						// 多重采样数量	
	basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;	// 多重采样每个像素质量级别
	basePsoDesc.DSVFormat = mDepthStencilFormat;								// 深度模板缓存格式

	/// PSO for 非透明物体
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
	// 如若指定为D3D12_DEPTH_WRITE_MASK_ZERO表示 搭配DepthEnable = true仍允许深度测试但禁止写入深度到深度缓存; 如若指定为D3D12_DEPTH_WRITE_MASK_ALL表示通过深度\模板测试的这个"深度数据"会被写进深度缓存
	// 注意,在本项目里,第二次写入的时候也无需写入深度缓存,原因是已经在法线渲染目标的绘制中写过一次场景深度了
	opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	// DepthFunc: 只接受枚举D3D12_COMPARISON_FUNC的成员, 以此来定义深度比较函数; 比较函数的利用原型公式是 stencilRef & stencilReadMask <| Value & stencilReadMask中的最中间那个符号,满足了就会返回TRUE
	// 本项目是 D3D12_COMPARISON_FUNC_EQUAL,意为 若给定像素的深度值 等于了 处在深度缓存里对应像素的深度, 就接受给定像素片段
	// !!!注意,这里以SSAO MAP第二次渲染场景时候,应该把深度检测的比较方法切换为EQUAL;由于只有距离眼睛最近的可视像素才能通过这项深度比较检测,以此来阻止第二次渲染过程的Overdraw
	opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));/// 创建出"非透明管线"

	/// PSO for 阴影图PASS
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;//继承自基管线
	/* 深度偏移相关设置, 公式详见603*/
	smapPsoDesc.RasterizerState.DepthBias = 100000;// 固定的偏移量
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;// 允许的最大深度偏移量,过大的倾斜会导致斜率偏移量过大,造成彼得潘失真
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;// 根据多边形斜率来控制偏移的缩放因子
	smapPsoDesc.pRootSignature = mRootSignature.Get();// 注意此时仍是:场景的根签名
	smapPsoDesc.VS =// 变更VS为 Shadow.hlsl里的VS
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.PS =// 变更PS为 Shadow.hlsl里不启用阿尔法测试的PS
	{
		reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
		mShaders["shadowOpaquePS"]->GetBufferSize()
	};
	/* !!!阴影图的渲染过程无需涉及渲染目标*/
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;// 重设为 未知RT格式
	smapPsoDesc.NumRenderTargets = 0;// 重设为没有渲染目标，禁止颜色输出
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));/// 创建出"非透明阴影管线"

	
	/// PSO for debug layer. 渲染阴影到面片管线
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
	debugPsoDesc.pRootSignature = mRootSignature.Get();// 仍设为场景的根签名
	debugPsoDesc.VS =// ShadowDebug.hlsl 我们需要单独一个shader来将采样阴影图并将其渲染到面片上，所以新建一个DebugShader
	{
		reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
		mShaders["debugVS"]->GetBufferSize()
	};
	debugPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
		mShaders["debugPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));/// 创建出"渲染阴影到面片管线"

	/// PSO for drawing normals. 渲染观察空间法向量管线
	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
	drawNormalsPsoDesc.VS =// DrawNormals.hlsl 这张shader负责把场景里各物体位于view space里的法向量渲染到与屏幕大小一致,格式一致的纹理内
	{
		reinterpret_cast<BYTE*>(mShaders["drawNormalsVS"]->GetBufferPointer()),
		mShaders["drawNormalsVS"]->GetBufferSize()
	};
	drawNormalsPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["drawNormalsPS"]->GetBufferPointer()),
		mShaders["drawNormalsPS"]->GetBufferSize()
	};
	drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;// !!!注意,此时RT格式重设为 SSAO里的法线Map格式
	drawNormalsPsoDesc.SampleDesc.Count = 1;// 重设采样数量为1
	drawNormalsPsoDesc.SampleDesc.Quality = 0;// 重设采样质量为0
	drawNormalsPsoDesc.DSVFormat = mDepthStencilFormat;// 仍然设深度模板格式为 基类框架里的DSV格式
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));/// 创建出"渲染观察空间法向量管线"

	/// PSO for SSAO. SSAO全屏四边形可及率管线
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = basePsoDesc;
	ssaoPsoDesc.InputLayout = { nullptr, 0 };			// !!!注意,重设输入布局为空
	ssaoPsoDesc.pRootSignature = mSsaoRootSignature.Get();// 重设根签名为 SSAO这种PASS的专用根签名
	ssaoPsoDesc.VS =// 在布置好观察空间法线和场景深度后,禁用深度缓存,并在每个像素处调用SSAO的像素着色器绘制一个全屏四边形,该shader生成一个对应的环境光可及率数据
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoVS"]->GetBufferPointer()),
		mShaders["ssaoVS"]->GetBufferSize()
	};
	ssaoPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoPS"]->GetBufferPointer()),
		mShaders["ssaoPS"]->GetBufferSize()
	};
	// SSAO 特效 不需要任何深度缓存
	ssaoPsoDesc.DepthStencilState.DepthEnable = false;// 禁用深度测试
	ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;// 不写入深度缓存
	ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;// RT格式重设为 Ssao::AmbientMapFormat环境光图格式
	ssaoPsoDesc.SampleDesc.Count = 1;
	ssaoPsoDesc.SampleDesc.Quality = 0;
	ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;// !!!深度模板格式重设为未知
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));///创建出 "SSAO全屏四边形可及率管线"

	/// PSO for SSAO 双边模糊特效管线
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
	ssaoBlurPsoDesc.VS =// SsaoBlur.hlsl 变远变模糊的SSAO(双边模糊) 让SSAO图的过渡更加平滑
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoBlurVS"]->GetBufferPointer()),
		mShaders["ssaoBlurVS"]->GetBufferSize()
	};
	ssaoBlurPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoBlurPS"]->GetBufferPointer()),
		mShaders["ssaoBlurPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));/// 创建出 "SSAO 双边模糊特效管线"

	/// PSO for 天空球管线
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;// !!!由于摄像机在天空球内部,所以要禁用裁剪
	/* 确保depth函数是LESS_EQUAL而不是LESS。否则，如果深度缓冲区被清除为1，则在z = 1 (NDC)处的标准化深度值将失败深度测试*/
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;// 远裁剪面是天空球，所以必须<=，天空球才不会被裁剪
	skyPsoDesc.pRootSignature = mRootSignature.Get();// 重设为 场景根签名
	skyPsoDesc.VS =//Sky.hlsl, 总是以摄像机作为天空球的中心， 摄像机动 天空球也跟着动,这样摄像机永远不会移到天空球外,造成穿帮
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));/// 创建出"天空球管线"
}

/// 构建3个帧资源并存到数组里 每帧里构造出出2个PassCB, 1个SSAOCB, 渲染项个数的ObjectCB, 5个结构体材质
void SsaoApp::BuildFrameResources()
{
	/* 构建3个帧资源并存到数组里
	 * 每帧里构造出出2个PassCB, 1个SSAOCB, 渲染项个数的ObjectCB, 5个结构体材质 
	 */
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 2/*Pass数*/, (UINT)mAllRitems.size()/*ObejctCB数*/, (UINT)mMaterials.size()));
	}
}

/// 构建出所有材质(注意材质序数和其使用的2D纹理位于堆的位置) 并 注册到全局材质表
void SsaoApp::BuildMaterials()
{
	/* 先列举初始化所有纹理的名字和路径*/
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",
		"tileDiffuseMap",
		"tileNormalMap",
		"defaultDiffuseMap",
		"defaultNormalMap",
		"skyCubeMap"
	};
	std::vector<std::wstring> texFilenames =
	{
		L"../../Textures/bricks2.dds",		// 0
		L"../../Textures/bricks2_nmap.dds", // 1
		L"../../Textures/tile.dds",			// 2
		L"../../Textures/tile_nmap.dds",	// 3
		L"../../Textures/white1x1.dds",		// 4		
		L"../../Textures/default_nmap.dds",	// 5
		L"../../Textures/sunsetcube1024.dds"// 6
	};

	/* bricks0: 砖块材质*/
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;// 材质的第几号缓存区, 可被外部数据源以CopyData()填充更新
	bricks0->DiffuseSrvHeapIndex = 0;// 漫反射纹理在堆中序数:0 --L"../../Textures/bricks2.dds",
	bricks0->NormalSrvHeapIndex = 1;// 法线纹理在堆中序数:1    --L"../../Textures/bricks2_nmap.dds",
	bricks0->DiffuseAlbedo = XMFLOAT4(0.7f, 0.5f, 0.5f, 0.9f);// 漫反射率
	bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);// 菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
	bricks0->Roughness = 0.3f;// 粗糙度(和菲涅尔系数一起用于控制镜面光),越大越粗糙

	/* tile0: 地板的材质*/
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 1;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->NormalSrvHeapIndex = 3;
	tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.4f, 1.0f);// 地板漫反射颜色偏黄色
	tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;

	/* mirror0 : 圆柱顶端的球体材质*/
	auto mirror0 = std::make_unique<Material>();
	mirror0->Name = "mirror0";
	mirror0->MatCBIndex = 3;
	mirror0->DiffuseSrvHeapIndex = 4;// 球体用的漫反射纹理用的是	L"../../Textures/white1x1.dds",		// 4
	mirror0->NormalSrvHeapIndex = 5;
	mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);// 球体颜色偏绿色
	mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
	mirror0->Roughness = 0.1f;
	/* skullMat: 骷髅头材质*/
	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 4;
	skullMat->DiffuseSrvHeapIndex = 4;// 骷髅头漫反射纹理用的是	L"../../Textures/white1x1.dds",		// 4
	skullMat->NormalSrvHeapIndex = 5;
	skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.9f, 1.0f);// 骷髅头颜色偏 蓝色
	skullMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
	skullMat->Roughness = 0.2f;
	/* 天空球材质*/
	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 5;
	sky->DiffuseSrvHeapIndex = 6;
	sky->NormalSrvHeapIndex = 7;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	/* 注册所有材质至 全局材质表*/
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["mirror0"] = std::move(mirror0);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["sky"] = std::move(sky);
}

/// 构建所有物体的渲染项(含天空球、面片、盒子、骷髅头、地板、两侧柱子)
void SsaoApp::BuildRenderItems()
{
	/* 构建 天空球 的渲染项,使用了CubeMap技术*/
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f) * XMMatrixTranslation(0.0f, 1000.0f, 0.0f));// 天空球的高度抬高1000单位
	skyRitem->TexTransform = MathHelper::Identity4x4(); // 天空球的 纹理变换矩阵 设为默认
	skyRitem->ObjCBIndex = 0;						    // 天空球的 物体索引设为0号
	skyRitem->Mat = mMaterials["sky"].get();			// 天空球的 材质使用 mMaterials["sky"]
	skyRitem->Geo = mGeometries["shapeGeo"].get();		// 天空球的 几何体使用mGeometries["shapeGeo"]
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;// 天空球的 图元类型使用三角形列表
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;// 天空球的 绘制三参数之索引数 设置为本渲染项的geo里的sphere子mesh的IndexCount
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;// 天空球的 绘制三参数之起始索引 设置为本渲染项的geo里的sphere子mesh的StartIndexLocation
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;// // 天空球的 绘制三参数之基准地址 设置为本渲染项的geo里的sphere子mesh的BaseVertexLocation
	// 渲染层级设置为 Sky,即使用了Cubemap技术的那一层 并把skyRitem注册到全局渲染项里
	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	/* 构建 面片 的渲染项,使用了阴影图技术*/
	auto quadRitem = std::make_unique<RenderItem>();
	quadRitem->World = MathHelper::Identity4x4();//默认
	quadRitem->TexTransform = MathHelper::Identity4x4();//默认
	quadRitem->ObjCBIndex = 1;							// 四边形面片 物体索引设为1号
	quadRitem->Mat = mMaterials["bricks0"].get();		// 随便给张材质
	quadRitem->Geo = mGeometries["shapeGeo"].get();
	quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
	quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;
	// 我们需要单独一个shader来将采样阴影图并将其渲染到面片上，所以新建一个DebugShader层级
	mRitemLayer[(int)RenderLayer::Debug].push_back(quadRitem.get());
	mAllRitems.push_back(std::move(quadRitem));

	/* 构建 盒子 的渲染项*/
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 0.5f, 1.0f));// 纹理变换矩阵自定义一下
	boxRitem->ObjCBIndex = 2;// 盒子物体索引设为2
	boxRitem->Mat = mMaterials["bricks0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	// 盒子 渲染层级设为非透明
	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	/* 构建 骷髅头 的渲染项*/
	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.4f, 0.4f, 0.4f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 3;// 骷髅头物体索引设为3
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	// 骷髅头渲染层级设为非透明
	mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	mAllRitems.push_back(std::move(skullRitem));

	/* 构建 地板 的渲染项*/
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 4;// 地板物体索引设为4
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	// 地板渲染项层级设为非透明
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	/* 构建 左右两列的物体 的渲染项*/
	XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
	UINT objCBIndex = 5;
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
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["mirror0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["mirror0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());
		// 依次注册进 全局渲染项表里
		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}
}

/// 绘制出指定层级的渲染项(物体)
void SsaoApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));// 255对齐单帧ObejctCB
	/* 取出单帧ObjectCB资源*/
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// 遍历所有渲染项(即物体)
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());// 管线上绑定此渲染项(此物体)的geo管理员里的 顶点buffer视图
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());			// 管线上绑定此渲染项(此物体)的geo管理员里的 索引buffer视图
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);				// 管线上绑定此渲染项的图元类型
		
		// 管线上绑定ObjectCB,设定为0号; 物体CB地址就是 当前帧的物体CB地址加上渲染项的物体索引*单物体大小
		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		// 使用绘制三参数绘制本次迭代的渲染项(本物体)
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

/// 绘制场景的深度 到一张关联ShadowMap的纹理内
void SsaoApp::DrawSceneToShadowMap()
{
	/* 设置视口和裁剪矩形*/
	mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	/* 将场景的深度图切换为写状态(渲染深度信息)*/
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	/* 清空深度 | 模板缓存*/
	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	/* 将RT 设置为空, 禁用向后台缓存写入像素颜色*/
	mCommandList->OMSetRenderTargets(0/*RT数*/, nullptr/*RT为空*/, false, &mShadowMap->Dsv());

	/* 255对齐PASSCB*/
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	/* 绑定ShadowMap 专用的PASSCB 到管线并绘制*/
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;// !!!阴影图PassCB GPU地址为：1个场景PassCB之后,才轮到ShadowMap的CB
	mCommandList->SetGraphicsRootConstantBufferView(1/*CB在管线上的索引*/, passCBAddress);		  // ShwdowMap PassCB管线上索引为1号

	/* 切换流水线为"ShadowMap Pass专用管线" 并绘制层级为非透明的渲染项*/
	mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	/* 将阴影图从写状态切换为读状态, 允许在shader里采样阴影图*/
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}

/// 绘制场景里 各物体位于观察空间的法线和深度到一张关联SSAO的纹理内
void SsaoApp::DrawNormalsAndDepth()
{
	/* 设置视口和裁剪矩形*/
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	/* 取出SSAO资源里的 法线map和法线RTV*/
	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	// 把SSAO里的法线图 从读切换为待处理渲染目标状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	/* 清除屏幕里所有的 normal map 和 深度缓存.*/
	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);// 此处是用法线图资源清理RTV
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);// 这里则是正常的深度模板清理

	// 绑定1个RT(normalMapRtv)到管线.向后台写入SSAO的法线像素颜色
	mCommandList->OMSetRenderTargets(1/*RT数*/, &normalMapRtv, true, &DepthStencilView());

	/* 为SSAO 渲染绑定所需要的常量缓存区*/
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress();// 注意这里的地址,还是之前工程里类似的地址
	mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

	/* 1.切换本次流水线为"DrawNormals.hlsl 这张shader", 其负责把场景里各物体位于view space里的法向量渲染到与屏幕大小一致,格式一致的纹理内 2.绘制层级为非透明*/
	mCommandList->SetPipelineState(mPSOs["drawNormals"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	/* 从待处理RT切换为只读状态, 让存有场景各物体法线的纹理 方便被采样*/
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}

/* 偏移SRV句柄到SRVHeap中指定的索引位置(CPU端)*/
CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetCpuSrv(int index)const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}
/* 偏移SRV句柄到SRVHeap中指定的索引位置(GPU端)*/
CD3DX12_GPU_DESCRIPTOR_HANDLE SsaoApp::GetGpuSrv(int index)const
{
	auto srvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srvHandle.Offset(index, mCbvSrvUavDescriptorSize);
	return srvHandle;
}
/* 偏移DSV句柄到DSVHeap中指定的索引位置(CPU端)*/
CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetDsv(int index)const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, mDsvDescriptorSize);
	return dsv;
}
/* 偏移RT句柄到RTHeap中指定索引位置*/
CD3DX12_CPU_DESCRIPTOR_HANDLE SsaoApp::GetRtv(int index)const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());// 从RTVHeap里拿 关联CPU的"渲染视图句柄"
	rtv.Offset(index, mRtvDescriptorSize);													 // 偏移RT句柄到堆中指定索引位置
	return rtv;
}

/// 组件持有6个元素的静态采样器数组
std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> SsaoApp::GetStaticSamplers()
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

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow
	};
}

