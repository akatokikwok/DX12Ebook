//***************************************************************************************
// LightingUtil.hlsl 
// 包含给着色器光照用的API
//***************************************************************************************

#define MaxLights 16// 预先设定光源数量不超过16个

// 光照结构体
struct Light
{
    float3 Strength;     // 光源的强度       
    float FalloffStart;  // 衰减起点.仅供点光\聚光灯使用
    float3 Direction;    // 照射方向.仅供平行光\聚光灯使用
    float FalloffEnd;    // 衰减终点.仅供点光\聚光灯使用
    float3 Position;     // 灯源位置.仅供点光\聚光灯使用
    float SpotPower;     // 仅供聚光灯使用
};

// 材质结构体
struct Material
{
    float4 DiffuseAlbedo;// 漫反照率
    float3 FresnelR0;    // 1个菲涅尔系数(就是施利克公式里的RF(0°))(和粗糙度一起用于控制镜面光)
    float Shininess;     // 1个粗糙度(和菲涅尔系数一起用于控制镜面光), 越大越粗糙 (这里取反,取光滑度)
};

// 常用辅助函数 CalcAttenuation:实现一种线性衰减因子的计算方法
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // 线性衰减
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// 此函数用于模拟菲涅尔方程的施利克近似,基于光向量L与表面法线n 之间的夹角
// 施利克近似法计算菲涅尔 Rf(θ) = Rf(0°) + (1-Rf(0°))(1-COSθ)^5,此公式中的Rf(0°)是介质的属性,不同材质此值均不同
// R0 = ( (n-1)/(n+1) )^2, 式子中的n是折射率.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

// 基于Roughness来模拟镜面反射的新函数
// S(θh)== (m+8/8) * COS^m(θh) == m+8/8 *(n· h)^m ;m越大越光滑, 镜面瓣会变窄
float3 BlinnPhong(float3 lightStrength /*光强*/, float3 lightVec /*光向量*/, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f; // Material结构体里的光滑度*256
    float3 halfVec = normalize(toEye + lightVec); // 半角向量 由toeye 和 光向量计算

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f; // 先预定1个粗糙因子
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec); // 再用施利克近似法计算菲涅尔因子

    float3 specAlbedo = fresnelFactor * roughnessFactor; // 高光系数由 菲涅尔因子和粗糙因子叠加而来

    // 本DEMO使用的是LDR而非HDR,但是镜面光仍会略微超出[0,1],所以要缩比例
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength; // 最终结果是 (材质的漫反射系数 + 高光系数) * 光强
}

/// 额外注意!!!!:operator* 令2个向量相乘表示的是"分量乘法"

//---------------------------------------------------------------------------------------
// 实现方向光
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L /*光源*/, Material mat, float3 normal, float3 toEye /*眼睛位置*/)
{
    // 光向量恰好与光源射进方向相反
    float3 lightVec = -L.Direction;

    //通过朗伯余弦定律按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f); // 光向量 点乘 法线计算出一个比例
    float3 lightStrength = L.Strength * ndotl; // 朗伯余弦定律按比例降低光强

    // 有了光强,就可以基于粗糙度模拟镜面反射
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// 实现点光
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos /*微表面某一点*/, float3 normal, float3 toEye)
{
    // 光向量 (从微表面1点指向光源位置)
    float3 lightVec = L.Position - pos;

    // d是由微表面到光源的距离
    float d = length(lightVec);

    // 点光的辐射范围检测,如若距离超出点光辐射阈值则不返回任何数值
    if (d > L.FalloffEnd)
        return 0.0f;

    // 规范化处理光向量
    lightVec /= d;

    // 通过朗伯余弦定律按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl; // 计算出来1个光强

    // 根据距离计算光量的衰减
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd); // 先算出1个衰减系数
    lightStrength *= att; // 光强乘以衰减系数后自身得到更新

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat); // 有了光强,就可以基于粗糙度模拟镜面反射
}

//---------------------------------------------------------------------------------------
// 计算聚光灯
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos /*微表面1点*/, float3 normal, float3 toEye)
{
    // 光向量
    float3 lightVec = L.Position - pos;

    // 点到光源距离
    float d = length(lightVec);

    // 辐射范围监测以及规范化处理光向量
    if (d > L.FalloffEnd)
        return 0.0f;
    lightVec /= d;

    // 类似方向光的形式, 先通过朗伯余弦定律按比例降低光强
    float ndotl = max(dot(lightVec, normal), 0.0f); // 光向量 点乘 法线计算出一个比例
    float3 lightStrength = L.Strength * ndotl; // 朗伯余弦定律按比例降低光强

    // 再根据点光的形式, 计算光量的衰减, 按比例降低光强
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // 根据聚光灯的光照模型对光强执行缩放处理
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat); // 有了光强,就可以基于粗糙度模拟镜面反射
}

/// 此函数用以计算微平面某点的光照方程
/// 多种光源允许叠加,数量被限制为16个,在光照数组里的优先级分别是 方向光>点光>聚光灯
float4 ComputeLighting(Light gLights[MaxLights] /*光源数组*/, Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

///===============如果程序需要在不同阶段支持不同数量的光源,那么只需要生成以不同#define来定义不同的shader即可==============

#if (NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}


