//***************************************************************************************
// Default.hlsl 执行本工程标准的VS和PS
// 其中要额外注意: 需要把场景中所有物体的位于观察空间法向量绘制到与sceen space等大,且格式为DXGI_FORMAT_R16G16B16A16_FLOAT的纹理图内;
// 同时还需要绑定持有场景深度的depth/stencil缓存;
// 以上2个步骤负责
// 第一部分: 观察空间法线与深度值的渲染过程
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

/* 经过顶点着色器处理后得到的顶点型*/
struct VertexOut
{
    float4 PosH : SV_POSITION;                             // 位于齐次裁剪空间的posH
    float4 ShadowPosH : POSITION0 /*注意语义是POSITION0*/;  // 变换到位于纹理空间的场景阴影图的ShadowPosH,借由posW被gShadowTransform变换得到
    float4 SsaoPosH : POSITION1 /*注意语义是POSITION1*/;    // 投影场景里的SSAO图而生成的投影纹理坐标SsaoPosH
    float3 PosW : POSITION2 /*注意语义是POSITION2*/;        // 位于世界空间的PosW, 也是微表面一点
    float3 NormalW : NORMAL;                               // 位于世界空间的法线
    float3 TangentW : TANGENT;                             // 位于世界空间的切线
    float2 TexC : TEXCOORD;                                // 纹理坐标
};

/* 顶点着色器负责 输出带有新属性的顶点实例*/
VertexOut VS(VertexIn vin)
{
    // 先默认初始化一下顶点实例
    VertexOut vout = (VertexOut) 0.0f;

	// 从结构化材质里 拿取每个物体独有的材质数据
    MaterialData matData = gMaterialData[gMaterialIndex];
	
    // 借由世界矩阵 把PosL变换到世界空间,得到微表面一点
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz; // 微表面一点
    // 借由世界矩阵 把NormalL变换到世界空间; 只做均匀缩放，所以可以不使用逆转置矩阵
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
	// 借由世界矩阵 把TangentU 从物体空间变换到世界空间;
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);

    // 借由"视图投影"叠加矩阵, 拿到位于齐次裁剪空间的顶点PosH
    vout.PosH = mul(posW, gViewProj);
	
	// 给各顶点的三角形进行"属性插值"
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform); // 让顶点里的TexC乘以gTexTransform, 参与计算的gTexTransform归属于cbPerObject
    vout.TexC = mul(texC, matData.MatTransform).xy;                 // 再将结果乘以每个物体里材质的变换矩阵并取xy分量得到 输出新的"顶点纹理坐标"

    // 将顶点 从世界空间变换到纹理空间,即乘以gShadowTransform变换矩阵 得到关联"阴影效果"的"场景阴影坐标"
    vout.ShadowPosH = mul(posW, gShadowTransform); // "场景阴影坐标", 参与计算的gShadowTransform归属于cbPass
    
    // 将顶点 从世界空间乘以gViewProjTex变换矩阵 得到关联"SSAO"的"SSAO投影纹理坐标"
    vout.SsaoPosH = mul(posW, gViewProjTex); // "SSAO投影纹理坐标", 参与计算的gViewProjTex归属于cbPass
	
    // 拿到最终计算出来的顶点实例
    return vout;
}

/* 像素着色器*/
float4 PS(VertexOut pin) : SV_Target
{
	// 从结构化材质里 拿取每个物体独有的 "结构化材质"数据 并暂存材质数据的这一系列属性
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;   // 暂存漫反照率
    float3 fresnelR0 = matData.FresnelR0;           // 暂存菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
    float roughness = matData.Roughness;            // 暂存粗糙度(和菲涅尔系数一起用于控制镜面光), 越大越粗糙
    uint diffuseMapIndex = matData.DiffuseMapIndex; // 暂存2D漫反射纹理查找索引
    uint normalMapIndex = matData.NormalMapIndex;   // 暂存法线纹理查找索引
	
    /* 这一部负责 采样漫反射纹理图 获取漫反照率颜色; 以"顶点的纹理坐标"为查找依据, 在2D纹理数组(含有漫反射纹理和法线纹理)中动态地查找并采样漫反射纹理, 并叠加乘到漫反照率颜色上*/
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC); // 最终计算出的漫反照率; 各向异性采样 纹理数组中的2D漫反射纹理 得到 漫反照率颜色

    /* 这一步负责 开启阿尔法测试; 阿尔法测试启动宏, 在程序里mShaders["shadowAlphaTestedPS"]这张shader开启了阿尔法测试*/
#ifdef ALPHA_TEST
    // 如若纹理中像素的颜色中的阿尔法通道值小于0.1, 就丢弃该像素; 应该尽快执行这项测试以便跳过无用功
    clip(diffuseAlbedo.a - 0.1f);
#endif

    /* 这一步负责 重新规范化法线; 因为对法线插值会有概率导致其非规范化*/
    pin.NormalW = normalize(pin.NormalW);
	
    /* 这一步负责得到 世界空间的法线(从切空间变换至世界空间)*/
    float4 normalMapSample = gTextureMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC); // 各向异性采样 纹理数组中的2D法线纹理 得到 法线图
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb/*法线图的rgb分量*/, pin.NormalW/*世界空间法线*/, pin.TangentW/*世界空间切线*/); // 把这张法线图里的法线值从切空间变换到世界空间

	// 解注释此行来 禁用法线纹理,并直接改用顶点里的法线
    //bumpedNormalW = pin.NormalW;

    // 从微表面1点指向眼睛(规范化)
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    /// 注意!!! (执行顶点 Shader的输出在Clip space) => 齐次裁剪空间 => (透视除法) => NDC空间 => (执行视口变换) => 屏幕空间 => (像素 Shader的输入在屏幕空间,而非NDC空间)
    /// Vertex Shader的输出在Clip Space，接着GPU会做透视除法变到NDC。这之后GPU还有一步，应用视口变换，转换到Screen Space，输入给Pixel Shader
    
    /* 这一步负责 处理SSAO投影纹理坐标以获取环境光可及率*/
    pin.SsaoPosH /= pin.SsaoPosH.w; // 除以w分量以此进入NDC空间
    float ambientAccess = gSsaoMap.Sample(gsamLinearClamp, pin.SsaoPosH.xy, 0.0f).r; // 以NDC下的 "SSAO投影纹理坐标的xy分量" 为采样依据, 采样SSAO纹理图, 拿取采样结果r分量得到环境光可及率

    /* 这一步负责 计算环境光颜色; 按比例缩放光照方程里的环境光选项*/
    float4 ambient = ambientAccess * gAmbientLight * diffuseAlbedo;// 计算最终环境光 == 可及率 * 每帧PASS环境光光源 * 漫反照颜色

    /* 这一步负责 构建PCF以获取阴影因子; 仅有第一个主光才投射阴影(即接收阴影图构建的PCF 得到的阴影因子)*/
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f); // 自定义1个阴影因子,float3型
    shadowFactor[0] = CalcShadowFactor(pin.ShadowPosH/*场景阴影坐标*/); // 让主光 接收 计算出来的阴影图PCF因子

    /* 这一步负责 计算直接光照颜色(它是点亮颜色的组成部分)*/
    const float shininess = (1.0f - roughness) * normalMapSample.a; // 注意这里的"光滑度"等于 1-粗糙度 再乘以 被采样的2D法线图的第一分量; 即理解为法线纹理的阿尔法影响光滑度
    Material mat = { diffuseAlbedo, fresnelR0, shininess }; // 利用{漫反照率颜色、菲涅尔R0、光滑度}构建一张材质模板
    // 使用接口计算直接光照
    float4 directLight = ComputeLighting(gLights /*光源数组*/,
        mat /*一张材质*/,
        pin.PosW/*微表面一点*/,
        bumpedNormalW /*世界空间法线*/,
        toEyeW /*指向眼睛的向量*/,
        shadowFactor /*接受了PCF阴影的主光阴影因子*/);
    
    /* 计算点亮光照 == 环境光颜色 + 直接光照颜色*/
    float4 litColor = ambient + directLight;

	/* 这一步负责 计算额外得镜面光环境反射颜色*/
    float3 r = reflect(-toEyeW, bumpedNormalW);// 求取 "视线的反射向量r"
    float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r); // 环境反射颜色; 由采样Cubemap以反射向量r为依据,得到环境反射颜色
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r); // 使用SchlickFresnel接口模拟一个"菲涅尔高光因子"; 此函数用于模拟菲涅尔方程的施利克近似,基于光向量L与表面法线n 之间的夹角
    
    /* 让点亮颜色再叠加计算出的"环境反射颜色"*/
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;// 点亮rgb颜色 + 光泽度*菲涅尔高光因子*采样cubemap得到的反射颜色rgb
	
    // 点亮颜色的透明度则从 "漫反照率"里取
    litColor.a = diffuseAlbedo.a;

    // 返回计算出的最终"点亮颜色"
    return litColor;
}


