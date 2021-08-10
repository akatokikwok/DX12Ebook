//***************************************************************************************
// DrawNormals.hlsl 这张shader负责把场景里各物体位于view space里的法向量渲染到与屏幕大小一致,格式一致的纹理内
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 0
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// 包含公用的.hlsl
#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;// local space的pos
    float3 NormalL : NORMAL;// local space的法线
    float2 TexC : TEXCOORD;// local space的纹理坐标,三角形插值用
    float3 TangentU : TANGENT;// local space的 切线
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

	// 暂存从结构体材质里获得 材质数据
    MaterialData matData = gMaterialData[gMaterialIndex];
	
    // 考虑到本项目里使用的是等比缩放来计算世界空间的法线和切线, 否则应该使用世界矩阵的逆转置矩阵执行计算法线
    vout.NormalW =  mul(vin.NormalL, (float3x3) gWorld);
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);

    // 把顶点变换至齐次裁剪空间
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gViewProj);
	
	// 为三角形插值而输出顶点属性
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);//先暂存 使用纹理变换矩阵变换后的顶点纹理坐标
    vout.TexC = mul(texC, matData.MatTransform).xy;                //再使用变换后的顶点纹理坐标 让其再被材质变换矩阵变换一次, 得到被插值三角形的顶点
	
    return vout;
}

/* 在像素着色器阶段,负责输出了物体的观察空间里的插值顶点法线, 而且使用的是浮点渲染目标来让写入浮点型数据变的合法*/
float4 PS(VertexOut pin) : SV_Target
{
	// 拿取结构体材质数据后,暂存各属性(漫反照率,漫反射贴图,法线贴图)
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
	
    // 使用各向异性采样2D漫反射贴图数组[10]来动态查找纹理
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

#ifdef ALPHA_TEST
    // 若启用了阿尔法测试宏,则丢弃纹理中像素阿尔法值小于0.1的像素;
    // 建议在shader的阶段尽早执行这项测试,从而优化性能
    clip(diffuseAlbedo.a - 0.1f);
#endif

	// 对法线插值有概率造成非规范化,要重新规范化处理
    pin.NormalW = normalize(pin.NormalW);
	
    // 注意,为SSAO使用插值顶点法线

    // 返回并写入物体位于观察空间的法线 float4型
    float3 normalV = mul(pin.NormalW, (float3x3) gView);
    return float4(normalV, 0.0f);
}


