/*
.fx文件VS会默认使用HLSL编译器对其进行编译，而.fx文件中并未定义main函数，所以会导致编译出错

右键.fx文件，“属性->配置属性->常规->项类型”，将“HLSL编译器”改为“不参与生成
*
*/
#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// Vertex结构体
struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};

// Constant结构体: 几何体, 里面存有浮点数型MVP矩阵, 成员接受外部写入
struct ObjectConstants
{
	XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();// MVP矩阵

};

/// 魔方类,继承自D3DApp
class BoxApp : public D3DApp
{
public:
	// 显式构造,调用基类构造器
	BoxApp(HINSTANCE hInstance);
	// 禁用拷贝构造和赋值构造
	BoxApp(const BoxApp& rhs) = delete;
	BoxApp& operator=(const BoxApp& rhs) = delete;
	// 析构
	~BoxApp();
	/// 重载框架方法: 为渲染程序编写初始化代码,例如分配资源,初始化对象和建立3D场景
	virtual bool Initialize()override;
private:
	/// 重载框架方法:当收到处理消息(或者窗口发生尺寸变化)的时候就需要调用
	virtual void OnResize()override;
	/// 重载框架方法:每一帧调用,更新3D渲染程序, 比如呈现动画,移动摄像机,检查输入
	virtual void Update(const GameTimer& gt)override;
	/// 重载框架方法:每一帧绘制的时候调用
	virtual void Draw(const GameTimer& gt)override;

	/// 重写基类鼠标操作; 便于重写鼠标输入消息的处理流程
	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;


	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildPSO();

private:
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;				// 根签名, ID3D12RootSignature型
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;					// 常量缓存的描述符堆 ID3d12DescriptorHeap型

	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr; // 常数缓存:几何体的Uploader,和常量缓存有关

	std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;					// Box几何体, 它直接持有场景里要绘制的模型

	ComPtr<ID3DBlob> mvsByteCode = nullptr;								// 顶点shader字节码
	ComPtr<ID3DBlob> mpsByteCode = nullptr;								// 像素shader字节码
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;					// 输入布局数组
	ComPtr<ID3D12PipelineState> mPSO = nullptr;							// 管线状态, ID3D12PipelineState型

	// 模型,视图,透视投影3个矩阵的初始化
	XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();
	// 一些旋转魔方用的参数
	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 5.0f;

	POINT mLastMousePos;												// 鼠标的最后绘制点
};

/// 程序运行的入口 WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
	// 针对调试版本开启运行时内存泄漏检查
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try {// 默认执行box类构造器 并检查类实例是否初始化 并执行类实例的run函数
		BoxApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();// 执行消息循环(非处理窗口消息的时候就执行每帧计算帧数, 更新场景, 绘制场景)
	}
	catch (DxException& e) {
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

// 仅调用基类构造器
BoxApp::BoxApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

// 析构器不作处理
BoxApp::~BoxApp()
{
}

/// 重写Initialize() 为渲染程序编写初始化代码,例如分配资源,初始化对象和建立3D场景
bool BoxApp::Initialize()
{
	// 先检查并执行基类的Initialize
	if (!D3DApp::Initialize())
		return false;

	// 1. 先借助分配器重置命令列表,复用内存, 为初始化命令做好准备
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	// 2. 构建常数缓存视图堆 / 构建CBV视图句柄 / 以根参数(此处选型为desciptor table)创建根签名 
	// / 编译shader字节码 填充inputlayout / 创建盒子模型 / 创建流水线状态
	BuildDescriptorHeaps();/// 初始化并创建出CBuffer的视图堆
	BuildConstantBuffers();/// 以Cbuffer描述符堆 返回第一个CBV句柄, 拿到CBV
	BuildRootSignature(); ///以根参数选型为CBV描述符table, 创建出根签名
	BuildShadersAndInputLayout();/// 编译各个着色器的字节码 和 填充输入布局数组
	BuildBoxGeometry();/// 创建盒子模型, 填充其顶点缓存和索引缓存,具体流程比较复杂,详见函数
	BuildPSO();/// 创建管线状态,由于PSO的验证比较耗时,推荐在初始化阶段就生成PSO

	// 3. 结束全部命令列表里命令的记录,打包成一个命令数组并添加到队列里执行
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };// 以命令列表裸指针初始化并得到一个命令列表数组
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);// 在队列里真正的执行命令列表

	// 4. 强制CPU等待GPU操作,刷新队列,等待GPU处理完所有事(使用了围栏技术)
	D3DApp::FlushCommandQueue();

	return true;
}

///重写OnResize():当收到这里在处理消息(或者窗口发生尺寸变化)的时候就需要调用, 主要是重建透视投影矩阵
void BoxApp::OnResize()
{
	// 1. 先调用基类OnReszie();
	D3DApp::OnResize();

	// 2. 检查用户是不是重新调整了窗口尺寸,更新宽高比并重新计算投影矩阵
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);// 重新构建透视矩阵
	XMStoreFloat4x4(&mProj, P);//存放透视矩阵到4x4浮点数里
}

/// 重写Update():每一帧调用,更新3D渲染程序, 比如呈现动画,移动摄像机,检查输入 ;每帧都构建新的MVP矩阵常量缓存
void BoxApp::Update(const GameTimer& gt)
{
	// 球坐标转化为笛卡尔坐标
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	// 制造一个观察矩阵,每一帧都要更新
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);

	// 组建MVP矩阵
	XMMATRIX world = XMLoadFloat4x4(&mWorld);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX worldViewProj = world * view * proj;

	// 以每一帧最新的MVP矩阵来更新常量缓存
	ObjectConstants objConstants;// 声明一个常量缓存对象(目前里面只存放世界矩阵)
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));// 以MVP转置矩阵来构造FLOAT4X4
	// 利用Uploader里的CopyData方法 将 "真正数据源.即MVP常数"
	// 复制到欲更新数据中, 以此达成从CPU端内存数据拷贝内存到常量缓存的目的
	mObjectCB->CopyData(0, objConstants);
}

/// 重载框架方法:每一帧绘制的时候调用
/// 每帧绘制调用的命令;
/// 1.重置分配器和重置命令列表来复用内存
/// 2设置视口\裁剪矩形
/// 3.后台缓存切换为渲染目标状态 并 清除后台缓存\深度缓存
/// 4.设置当前RTV,DSV为渲染目标
/// 5.依据CBVHeap列表里设置常量缓存描述符堆
/// 6.设置根签名
/// 7.设置盒子模型的顶点缓存(需要视图)\索引缓存(需要视图) 设置图元为三角形列表
/// 8.设置常量缓存的 DescriptorTable
/// 9.按索引绘制
/// 10.切换为呈现状态 并关闭命令记录
/// 11.队列执行命令
/// 12.交换链里图像呈现到前台,并刷新最新的后台缓存序数
/// 13.强制CPU等待GPU 同步
void BoxApp::Draw(const GameTimer& gt)
{
	// 1.1  Reset分配器, 复用记录命令所用的内存
	// 注意!! 只有当GPU中所有的命令列表都执行完, 才可以重置分配器
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// 1.2 复用命令列表内存(需要分配器和PSO)
	// 当利用函数ExecuteCommandList把列表都加入队列之后,就可以重置列表
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

	// 2. 设置视口和裁剪矩形,这步骤独立于PSO
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// 3.1 依据资源的使用用途 指示其状态切换,此处把资源(后台缓存)从呈现切换为渲染目标状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// 3.2 清除后台缓存view (渲染目标) 和 深度缓存view
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::SteelBlue, 0, nullptr);// 用深蓝色
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// 3.3 在管线上设置 欲渲染的RenderTargets(此处是后台缓存view), 需要两个视图(RTV和DSV)的句柄(偏移查找出来)
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// 4. 用字段里的constant view heap初始化1个 堆数组,再把视图堆数组设置到管线上
	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 5.1 先设置CBV的root signature到管线上
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	// 5.2 依次在管线上设置 魔方几何体的 vertex buffer / index buffer/ 图元
	mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());// 设置顶点缓存,需要Meshgeometry类的成员顶点缓存视图
	mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());// 设置索引缓存,同样的也是需要Meshgeometry类的成员索引缓存视图
	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);// 设置图元类型为三角形列表;

	// 5.3 设置CBV的desciptor table与管线相绑定(需要字段视图堆的首句柄,并指定寄存器槽号)
	mCommandList->SetGraphicsRootDescriptorTable(0/*寄存器槽号*/, mCbvHeap->GetGPUDescriptorHandleForHeapStart()/*描述符堆中的首个view句柄*/);

	// 6. 按索引绘制魔方 DrawIndexedInstanced (注意各参数意义)
	mCommandList->DrawIndexedInstanced(
		mBoxGeo->DrawArgs["box"].IndexCount,//SubmeshGeometry结构体类型里的索引数量
		1, // 实例化高级技术,默认设为1
		0, // 每个模型相对于全局索引索引缓存里的起始索引序数,这里只有一个模型,设为0
		0, // 每个模型的第一个顶点相对于全局顶点缓存的位置,即"基准顶点地址"
		0  // 实例化高级技术相关,默认设为0
	);

	// 7. 后台缓存状态切换,上述命令处理完了之后,在这里把资源从渲染目标状态切换为呈现状态
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// 8. 完成大部分命令的记录,关闭命令列表记录 / 向队列里添加欲执行的命令列表
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 9. 真正Present 后台缓存, 并在交换链中交换前台和后台缓存(做法是更新此刻的后台缓存序数)
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// 10. 强制CPU等待GPU,刷新命令队列
	FlushCommandQueue();
}

/* 按下鼠标, 主要是设置最后鼠标的坐标并同时捕捉一下主窗口句柄*/
void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);// 记得捕捉主窗口句柄
}

/* 抬起鼠标,就释放捕捉的主窗口句柄*/
void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

/* 鼠标移动的操作*/
void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0) {
		// 根据鼠标左键移动的举例 计算旋转角度, 每个像素都按照此角度的 1/4执行旋转
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	} else if ((btnState & MK_RBUTTON) != 0) {
		// 按下鼠标右键的时候让每个像素按照鼠标移动距离的0.005倍缩放
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// 限制可视半径的范围
		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

/* 初始化并创建出cbuffer的 描述符堆资源 即字段 mCBVHeap*/
void BoxApp::BuildDescriptorHeaps()
{
	// 声明一个cbv 的描述符堆DESC
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;// 注意此混合堆类型
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	// 用D3D设备创建出 constantBuffer的描述符堆
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

///以Cbuffer描述符堆 返回第一个CBV句柄, 拿到CBV
void BoxApp::BuildConstantBuffers()
{
	// 1. 为字段mObjectCB 构建1个 常数缓存的 uploader资源
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

	// 2. 255自己对齐, 将单个constant结构体的字节大小 优化为255字节的整数倍
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// 3. 拿取uploader实例缓存区的GPU起始地址(即索引序数为0的那个缓存区),使用裸指针的GetGPUVirtualAddress获得
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

	// 4. 组建偏移到常量缓存里绘制第 0 个模型所需要的常量数据
	int boxCBufIndex = 0;// 暂设目前的模型 是序数为0 的索引缓存区(因为可能以后会有十几个模型,就有n个缓存区)
	cbAddress += boxCBufIndex * objCBByteSize;// 序数 * 单缓存区大小 再加上起始缓存区地址 得到最后地址

	// 5.1 填充常量缓存DESC, 需要 "真正的缓存区地址" 和 "单缓存区字节大小" , 必须被优化为255字节的整数倍
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;// "常量缓存视图描述"的BufferLocation来对应 常量数据集里第 i 个物体的常量数据
	cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	// 5.2 从已有的常量堆里创建出constant buffer view
	md3dDevice->CreateConstantBufferView(
		&cbvDesc,
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

/* 以根参数选型为CBV描述符table
   并创建出根签名
*/
void BoxApp::BuildRootSignature()
{
	/// 1.声明1个根参数数组和一个视图table,并把 持有1个CBV的 descrptior table绑定到常量缓存区 的寄存器0号,即HLSL中的register(b0)
	// 声明一个带1个元素的 根参数数组
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];
	// 结合上述根参数数组 选型并创建出一个只有1个CBV的描述符table
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV/*描述符表的类型*/, 1/*这张表里的描述符的数量*/, 0/*绑定到HLSL着色器的槽位序数*/);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	/// 2. 利用rootSigDesc初始化一个根签名里的blob槽位
	// 根签名就是根参数的数组, 初始化一个根签名DESC
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	// 创建仅含1个slot的序列化Blob,即此处的serializedRootSig,(这个slot指向1个仅有单个CBUFFER组成的描述符) 
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()
	);
	// 检查errorBlob
	if (errorBlob != nullptr) {
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	/// 3. serializedRootSig->GetBufferPointer(),创建出最终的根签名
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)
	));
}

/* 编译各个着色器的字节码 和 填充输入布局数组*/
void BoxApp::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	// 编译着色器字节码
	mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

	// 填充输入布局数组(目前仅给POSITION 和 COLOR)
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

/// 创建盒子模型, 填充其顶点缓存和索引缓存,具体流程比较复杂,详见函数
/* 针对盒子模型
* 初始化索引数组和顶点数组
* 初始化顶点字节总大小,索引字节总大小
* 构造盒子模型并设置名称
* 分别为盒子模型的顶点和索引创建BLOB内存 并以顶点数据和索引数据填充
* 在盒子模型的blob内存填充过之后, 借助工具方法创造出上传顶点BUFFER和上传索引BUFFER
* 初始化一个submesh并以其更新盒子模型的无序表
*/
void BoxApp::BuildBoxGeometry()
{
	// 1.1 以Vertex结构体 初始化"顶点数组", 它是1个数据源
	std::array<Vertex, 8> vertices =
	{
		Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
		Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
	};

	// 1.2 初始化"索引数组", 它也是1个数据源
	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};
	// 2. 拿到Vertex buffer和Index buffer的总字节大小
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);			// 拿到8个顶点的字节大小,它们就是vertexbuffer字节尺寸
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);	// 索引数据总大小
	// 3.1 构建魔方几何体 并 设置 魔方名字
	mBoxGeo = std::make_unique<MeshGeometry>();// 构造模型,此处是MESHGeometry型
	mBoxGeo->Name = "boxGeo";// 设置此模型名字为"boxGeo"

	// 4.1 先为魔方的CPU端 的VertexBufferCPU成员 开辟出一块Blob内存,// 顶点数组数据内容 及 大小被拷贝至 模型的 VertexBufferCPU成员里
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
	CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer()/*拷贝终点*/, vertices.data()/*数据源*/, vbByteSize/*数据大小*/);

	// 4.2 再为魔方的CPU端 的IndexBufferCPU成员 开辟出一块Blob内存,// index数组数据内容 及 大小被拷贝至 模型的 VertexBufferCPU成员里
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
	CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	// 5.1 借助工具函数CreateDefaultBuffer来构建盒子模型里的 位于中介位置的 VertexUploadBuffer
	mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);

	// 5.2 使用索引data, 来借助工具方法来构建盒子模型里的 位于中介位置的 indexBufferUploader
	mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);
	// 6. 魔方的杂项设置
	mBoxGeo->VertexByteStride = sizeof(Vertex);// 设置模型的 单顶点字节大小(偏移)
	mBoxGeo->VertexBufferByteSize = vbByteSize;// 设置模型的 顶点字节总大小
	mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;// 设置模型的 索引格式
	mBoxGeo->IndexBufferByteSize = ibByteSize;// 设置模型的 索引字节总大小

	// 7.1 设置一下submesh的字段
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	// 7.2 填充魔方<string - submesh>无序表
	mBoxGeo->DrawArgs["box"] = submesh;
}

/// 创建管线状态
void BoxApp::BuildPSO()
{
	// 声明管线DESC
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));// 先默认步骤,清空一下流水线
	psoDesc.InputLayout =							// 顶点输入布局描述
	{
		mInputLayout.data(),
		(UINT)mInputLayout.size()
	};
	psoDesc.pRootSignature = mRootSignature.Get();	// 指向1个与此PSO相绑定的根签名的指针
	psoDesc.VS =									// 待绑定的Vertex Shader
	{
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	};
	psoDesc.PS =									// 待绑定的Pixel Shader
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);// 指定光栅化状态
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);// 指定混合状态
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);// 配置深度模板状态
	psoDesc.SampleMask = UINT_MAX;// 设置每个采样点的采集情况
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;// 图元拓扑类型
	psoDesc.NumRenderTargets = 1;										// 同时渲染用到的渲染目标数量
	psoDesc.RTVFormats[0] = mBackBufferFormat;							// 渲染目标格式
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;					// 多重采样数量	
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;// 多重采样每个像素质量级别
	psoDesc.DSVFormat = mDepthStencilFormat;							// 深度模板缓存格式
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}