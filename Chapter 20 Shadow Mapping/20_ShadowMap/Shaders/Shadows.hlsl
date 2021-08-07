//***************************************************************************************
// Shadows.hlsl这个是阴影图的shader
// 绘制shadowMap的shader很简单，因为不输出颜色，所以像素着色器为空，顶点着色器只需做基本的空间变换即可。
//***************************************************************************************

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	MaterialData matData = gMaterialData[gMaterialIndex];// 取出对应序数的结构体材质

	/* 先把顶点变换到齐次裁剪空间*/
	float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
	vout.PosH = mul(posW, gViewProj);

	/* 经过三角形差值输出顶点属性*/
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	return vout;
}

// This is only used for alpha cut out geometry, so that shadows 
// show up correctly.  Geometry that does not need to sample a
// texture can use a NULL pixel shader for depth pass.
void PS(VertexOut pin)
{
	/* 先取出特定的结构里材质里的 属性(这里是漫反射率, 2D漫反射贴图)*/
	MaterialData matData = gMaterialData[gMaterialIndex];// 取出对应序数的结构体材质
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	uint diffuseMapIndex = matData.DiffuseMapIndex;

	// 各向异性采样2D纹理数组以 动态地查找漫反射纹理,并返回最终的颜色赋给漫反射率
	diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

#ifdef ALPHA_TEST
	// Discard pixel if texture alpha < 0.1.  We do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffuseAlbedo.a - 0.1f);
#endif
}


