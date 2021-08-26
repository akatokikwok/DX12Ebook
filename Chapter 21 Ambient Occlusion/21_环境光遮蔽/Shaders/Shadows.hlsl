//***************************************************************************************
// Shadows.hlsl这个是阴影图的shader
// 绘制shadowMap的shader很简单，因为不输出颜色，所以像素着色器为空，顶点着色器只需做基本的空间变换即可.
//***************************************************************************************

// 引用 common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

// ShdowMap要的顶点实例
struct VertexOut
{
    float4 PosH : SV_POSITION;// 阴影图的每顶点"齐次裁剪空间位置"
    float2 TexC : TEXCOORD;   // 阴影图的每个顶点"纹理坐标"
};

// 顶点着色器只需做基本的空间变换即可
VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    MaterialData matData = gMaterialData[gMaterialIndex];// 取出每个物体独有的结构化材质数据
	
    // 将顶点 变换到世界空间
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    // 将顶点变换到齐次裁剪空间
    vout.PosH = mul(posW, gViewProj);// "齐次裁剪空间顶点"
	
	// 给各顶点的三角形进行"属性插值"
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform); // 让顶点里的纹理坐标乘以gTexTransform变换矩阵
    vout.TexC = mul(texC, matData.MatTransform).xy; // 再将结果乘以每个物体里材质的变换矩阵并取xy分量得到 输出的"顶点纹理坐标"
	
    return vout;
}

// 因为不输出颜色，所以像素着色器大多逻辑为空
void PS(VertexOut pin)
{
	// 从结构化材质里 拿取每个物体独有的 "结构化材质"数据 并暂存材质数据的这一系列属性
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
	
	// 以"顶点的纹理坐标"为查找依据, 在2D纹理数组(含有漫反射纹理和法线纹理)中动态地查找并采样漫反射纹理, 并叠加乘到漫反照率颜色上
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

    // 阿尔法测试启动宏
#ifdef ALPHA_TEST
    // 如若纹理中像素的颜色中的阿尔法通道值小于0.1, 就丢弃该像素; 应该尽快执行这项测试以便跳过无用功
    clip(diffuseAlbedo.a - 0.1f);
#endif
}


