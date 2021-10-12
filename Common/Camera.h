//***************************************************************************************
// 关注2种东西:一种是 观察坐标系的原点,xyz轴; 第二种是摄像机的视锥体  
//***************************************************************************************

#ifndef CAMERA_H
#define CAMERA_H

#include "d3dUtil.h"

class Camera
{
public:

	Camera();
	~Camera();

	// 获取及设置世界坐标系中 摄像机的位置
	DirectX::XMVECTOR GetPosition()const;
	DirectX::XMFLOAT3 GetPosition3f()const;
	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& v);

	// 获取摄像机的各个基向量
	DirectX::XMVECTOR GetRight()const;
	DirectX::XMFLOAT3 GetRight3f()const;
	DirectX::XMVECTOR GetUp()const;
	DirectX::XMFLOAT3 GetUp3f()const;
	DirectX::XMVECTOR GetLook()const;
	DirectX::XMFLOAT3 GetLook3f()const;

	// 获取视锥体各属性
	float GetNearZ()const;
	float GetFarZ()const;
	float GetAspect()const;
	float GetFovY()const;
	float GetFovX()const;

	// 获取近远平面的宽高
	float GetNearWindowWidth()const;
	float GetNearWindowHeight()const;
	float GetFarWindowWidth()const;
	float GetFarWindowHeight()const;

	// 设置视锥体
	void SetLens(float fovY, float aspect, float zn, float zf);

	// 定义摄像机空间
	void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
	void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

	// 获取观察矩阵和投影矩阵
	DirectX::XMMATRIX GetView()const;
	DirectX::XMMATRIX GetProj()const;

	DirectX::XMFLOAT4X4 GetView4x4f()const;
	DirectX::XMFLOAT4X4 GetProj4x4f()const;

	// 对摄像机执行步长为d的左右平移或者前后移动
	void Strafe(float d);
	void Walk(float d);

	// 旋转摄像机
	void Pitch(float angle);
	void RotateY(float angle);

	// 修改了摄像机的朝向和位置之后,调用此方法重构观察矩阵
	void UpdateViewMatrix();

private:

	// 以世界坐标系表示的 有关观察空间坐标系 的 原点、x轴、y轴、z轴
	DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };// x
	DirectX::XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };// y
	DirectX::XMFLOAT3 mLook = { 0.0f, 0.0f, 1.0f };// z

	// 有关视锥体的属性
	float mNearZ = 0.0f;
	float mFarZ = 0.0f;
	float mAspect = 0.0f;
	float mFovY = 0.0f;
	float mNearWindowHeight = 0.0f;// 近裁剪面高度 = 2 * tan(mFovY) * mNearZ
	float mFarWindowHeight = 0.0f; // 远裁剪面高度 = 2 * tan(mFovY) * mFarZ

	bool mViewDirty = true;

	// 缓存观察矩阵 与 投影矩阵
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();
};

#endif // CAMERA_H