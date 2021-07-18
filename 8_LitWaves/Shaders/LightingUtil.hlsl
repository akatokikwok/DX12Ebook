#define MaxLights 16 //Ԥ���趨��Դ����������16��

// HLSL�нṹ��Ҫ4D��������
struct Light
{
    float3 Strength;     // ��Դ��ǿ��
    float  FalloffStart; // �������\�۹��ʹ��
    float3 Direction;    // ����ƽ�й�\�۹��ʹ��
    float  FalloffEnd;   // �������\�۹��ʹ��
    float3 Position;     // �������\�۹��ʹ��
    float SpotPower;     // �����۹��ʹ��
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float  Shininess;
};

// ���ø������� CalcAttenuation:ʵ��һ������˥�����ӵļ��㷽��
float CalcAttenuation(float d/*˥������*/, float falloffStart, float falloffEnd)
{
    // ����˥��
    return saturate((falloffEnd-d) / (falloffEnd - falloffStart));
}

// �˺�������ģ����������̵�ʩ���˽���,���ڹ�����L����淨��n ֮��ļн�
// ʩ���˽��Ʒ���������� Rf(��) = Rf(0��) + (1-Rf(0��))(1-COS��)^5,�˹�ʽ�е�Rf(0��)�ǽ��ʵ�����,��ͬ���ʴ�ֵ����ͬ
// R0 = ( (n-1)/(n+1) )^2, ʽ���е�n��������.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0)*(f0*f0*f0*f0*f0);

    return reflectPercent;
}

// ����Roughness��ģ�⾵�淴����º���
// S(��h)== (m+8/8) * COS^m(��h) == m+8/8 *(n�� h)^m ;mԽ��Խ�⻬, �������խ
float3 BlinnPhong(float3 lightStrength/*��ǿ*/, float3 lightVec/*������*/, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;// Material�ṹ����Ĺ⻬��*256
    float3 halfVec = normalize(toEye + lightVec);// ������� ��toeye �� ����������

    float roughnessFactor = (m + 8.0f)*pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;// ��Ԥ��1���ֲ�����
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);// ����ʩ���˽��Ʒ��������������

    float3 specAlbedo = fresnelFactor*roughnessFactor;// �߹�ϵ���� ���������Ӻʹֲ����ӵ��Ӷ���

    // ��DEMOʹ�õ���LDR����HDR,���Ǿ�����Ի���΢����[0,1],����Ҫ������
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;// ���ս���� (���ʵ�������ϵ�� + �߹�ϵ��) * ��ǿ
}
/// ����ע��!!!!:operator* ��2��������˱�ʾ����"�����˷�"

//---------------------------------------------------------------------------------------
// ʵ�ַ����
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L/*��Դ*/, Material mat, float3 normal, float3 toEye/*�۾�λ��*/)
{
    // ������ǡ�����Դ��������෴
    float3 lightVec = -L.Direction;

    //ͨ���ʲ����Ҷ��ɰ��������͹�ǿ
    float ndotl = max(dot(lightVec, normal), 0.0f); // ������ ��� ���߼����һ������
    float3 lightStrength = L.Strength * ndotl;      // �ʲ����Ҷ��ɰ��������͹�ǿ

    // ���˹�ǿ,�Ϳ��Ի��ڴֲڶ�ģ�⾵�淴��
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// ʵ�ֵ��
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos/*΢����ĳһ��*/, float3 normal, float3 toEye)
{
    // ������ (��΢����1��ָ���Դλ��)
    float3 lightVec = L.Position - pos;

    // d����΢���浽��Դ�ľ���
    float d = length(lightVec);

    // ���ķ��䷶Χ���,�������볬����������ֵ�򲻷����κ���ֵ
    if(d > L.FalloffEnd)
        return 0.0f;

    // �淶�����������
    lightVec /= d;

    // ͨ���ʲ����Ҷ��ɰ��������͹�ǿ
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;// �������1����ǿ

    // ���ݾ�����������˥��
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd); // �����1��˥��ϵ��
    lightStrength *= att;                                         // ��ǿ����˥��ϵ��������õ�����

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat); // ���˹�ǿ,�Ϳ��Ի��ڴֲڶ�ģ�⾵�淴��
}

//---------------------------------------------------------------------------------------
// ����۹��
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos/*΢����1��*/, float3 normal, float3 toEye)
{
    // ������
    float3 lightVec = L.Position - pos;

    // �㵽��Դ����
    float d = length(lightVec);

    // ���䷶Χ����Լ��淶�����������
    if(d > L.FalloffEnd)
        return 0.0f;
    lightVec /= d;

    // ���Ʒ�������ʽ, ��ͨ���ʲ����Ҷ��ɰ��������͹�ǿ
    float ndotl = max(dot(lightVec, normal), 0.0f); // ������ ��� ���߼����һ������
    float3 lightStrength = L.Strength * ndotl; // �ʲ����Ҷ��ɰ��������͹�ǿ

    // �ٸ��ݵ�����ʽ, ���������˥��, ���������͹�ǿ
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // ���ݾ۹�ƵĹ���ģ�ͶԹ�ǿִ�����Ŵ���
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat); // ���˹�ǿ,�Ϳ��Ի��ڴֲڶ�ģ�⾵�淴��
}

/// �˺������Լ���΢ƽ��ĳ��Ĺ��շ���
/// ���ֹ�Դ�������,����������Ϊ16��,�ڹ�������������ȼ��ֱ��� �����>���>�۹��
float4 ComputeLighting(Light gLights[MaxLights]/*��Դ����*/, Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

///===============���������Ҫ�ڲ�ͬ�׶�֧�ֲ�ͬ�����Ĺ�Դ,��ôֻ��Ҫ�����Բ�ͬ#define�����岻ͬ��shader����==============
    
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


