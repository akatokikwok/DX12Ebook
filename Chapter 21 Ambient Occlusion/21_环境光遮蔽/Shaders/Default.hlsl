//***************************************************************************************
// Default.hlsl by Frank Luna (C) 2015 All Rights Reserved.
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
    float4 PosH : SV_POSITION;                           // 位于齐次裁剪空间的posH
    float4 ShadowPosH : POSITION0 /*注意语义是POSITION0*/;// 变换到位于纹理空间的场景阴影图的ShadowPosH,借由posW被gShadowTransform变换得到
    float4 SsaoPosH : POSITION1   /*注意语义是POSITION1*/;// 投影场景里的SSAO图而生成的投影纹理坐标SsaoPosH
    float3 PosW : POSITION2       /*注意语义是POSITION2*/;// 位于世界空间的PosW
    float3 NormalW : NORMAL;                             // 位于世界空间的法线
    float3 TangentW : TANGENT;                           // 位于世界空间的切线
    float2 TexC : TEXCOORD;                              // 纹理坐标
};

VertexOut VS(VertexIn vin)
{
    // 先默认初始化一下顶点实例
    VertexOut vout = (VertexOut) 0.0f;

	// 从结构化材质里 拿取每个物体独有的材质数据
    MaterialData matData = gMaterialData[gMaterialIndex];
	
    // 借由世界矩阵 把PosL变换到世界空间
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    // 借由世界矩阵 把NormalL变换到世界空间; 只做均匀缩放，所以可以不使用逆转置矩阵
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
	// 借由世界矩阵 把TangentU 从物体空间变换到世界空间;
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);

    // 借由"视图投影"叠加矩阵, 拿到位于齐次裁剪空间的顶点PosH
    vout.PosH = mul(posW, gViewProj);

    // 在顶点着色器里,为投影场景里的SSAO图而生成的投影纹理坐标
    vout.SsaoPosH = mul(posW, gViewProjTex);
	
	// Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;

    // Generate projective tex-coords to project shadow map onto scene.
    // 把顶点从世界空间变换到纹理空间,通过投影取得场景阴影图的效果
    vout.ShadowPosH = mul(posW, gShadowTransform);
	
    // 拿到最终计算出来的顶点实例
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Fetch the material data.
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
	
    // Dynamically look up the texture in the array.
    diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

#ifdef ALPHA_TEST
    // Discard pixel if texture alpha < 0.1.  We do this test as soon 
    // as possible in the shader so that we can potentially exit the
    // shader early, thereby skipping the rest of the shader code.
    clip(diffuseAlbedo.a - 0.1f);
#endif

	// Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);
	
    float4 normalMapSample = gTextureMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalW, pin.TangentW);

	// Uncomment to turn off normal mapping.
    //bumpedNormalW = pin.NormalW;

    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    // 在像素着色器里,完成纹理投影并对SSAO图进行采样拿到可及率
    pin.SsaoPosH /= pin.SsaoPosH.w;
    float ambientAccess = gSsaoMap.Sample(gsamLinearClamp, pin.SsaoPosH.xy, 0.0f).r;

    // 根据采样数据按比例缩放光照方程里的环境光选项
    float4 ambient = ambientAccess * gAmbientLight * diffuseAlbedo;//最终环境光 == 可及率 * 环境光光源 * 漫反照率

    // Only the first light casts a shadow.
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
    shadowFactor[0] = CalcShadowFactor(pin.ShadowPosH);

    const float shininess = (1.0f - roughness) * normalMapSample.a;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        bumpedNormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

	// Add in specular reflections.
    float3 r = reflect(-toEyeW, bumpedNormalW);
    float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;
	
    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


