//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
	SetLens(0.25f*MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
}

Camera::~Camera()
{
}

XMVECTOR Camera::GetPosition()const
{
	return XMLoadFloat3(&mPosition);
}

XMFLOAT3 Camera::GetPosition3f()const
{
	return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
	mPosition = XMFLOAT3(x, y, z);
	mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
	mPosition = v;
	mViewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
	return XMLoadFloat3(&mRight);
}

XMFLOAT3 Camera::GetRight3f()const
{
	return mRight;
}

XMVECTOR Camera::GetUp()const
{
	return XMLoadFloat3(&mUp);
}

XMFLOAT3 Camera::GetUp3f()const
{
	return mUp;
}

XMVECTOR Camera::GetLook()const
{
	return XMLoadFloat3(&mLook);
}

XMFLOAT3 Camera::GetLook3f()const
{
	return mLook;
}

float Camera::GetNearZ()const
{
	return mNearZ;
}

float Camera::GetFarZ()const
{
	return mFarZ;
}

float Camera::GetAspect()const
{
	return mAspect;
}

float Camera::GetFovY()const
{
	return mFovY;// 得到垂直视场角α,单位为°
}

float Camera::GetFovX()const
{
	float halfWidth = 0.5f*GetNearWindowWidth();// 已知近裁剪面全宽, 可得半宽
	return 2.0f*atan(halfWidth / mNearZ);//得到水平视场角β,单位为°,即β = 2 * arctan(半宽 / 近裁剪面距离)
}
/// 计算近裁剪面宽度
float Camera::GetNearWindowWidth()const
{
	return mAspect * mNearWindowHeight;// 宽高比 乘以 高 等于 宽
}
/// 仅拿取近裁剪面高度
float Camera::GetNearWindowHeight()const
{
	return mNearWindowHeight;// 仅拿到高
}

float Camera::GetFarWindowWidth()const
{
	return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
	return mFarWindowHeight;
}

/// 缓存视锥体属性和构建投影矩阵的时候就需要用到此方法
void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	// cache properties
	mFovY = fovY;// 垂直视场角
	mAspect = aspect;// 宽高比
	mNearZ = zn;// 近裁剪面到眼睛距离
	mFarZ = zf; // 远裁剪面到眼睛距离

	mNearWindowHeight = 2.0f * mNearZ * tanf( 0.5f*mFovY );// 近裁剪面高度 = 2 * tan(mFovY) * mNearZ
	mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f * mFovY);  // 远裁剪面高度 = 2 * tan(mFovY) * mFarZ

	XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY/*垂直视场角*/, mAspect/*宽高比*/, mNearZ/*近裁剪面*/, mFarZ/*远裁剪面*/);// XMMatrixPerspectiveFovLH构建投影矩阵
	XMStoreFloat4x4(&mProj, P);// 投影矩阵存到4x4里
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
	XMVECTOR U = XMVector3Cross(L, R);

	XMStoreFloat3(&mPosition, pos);
	XMStoreFloat3(&mLook, L);
	XMStoreFloat3(&mRight, R);
	XMStoreFloat3(&mUp, U);

	mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = XMLoadFloat3(&pos);
	XMVECTOR T = XMLoadFloat3(&target);
	XMVECTOR U = XMLoadFloat3(&up);

	LookAt(P, T, U);

	mViewDirty = true;
}

XMMATRIX Camera::GetView()const
{
	assert(!mViewDirty);
	return XMLoadFloat4x4(&mView);
}

XMMATRIX Camera::GetProj()const
{
	return XMLoadFloat4x4(&mProj);
}


XMFLOAT4X4 Camera::GetView4x4f()const
{
	assert(!mViewDirty);
	return mView;
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
	return mProj;
}

void Camera::Strafe(float d)
{
	// mPosition += d*mRight
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR r = XMLoadFloat3(&mRight);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));

	mViewDirty = true;
}

void Camera::Walk(float d)
{
	// mPosition += d*mLook;即摄像机位置 沿着look方向执行移动
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = XMLoadFloat3(&mLook);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));// 第一个乘第二个后加上第三个

	mViewDirty = true;
}

/// Pitch俯仰观察
void Camera::Pitch(float angle)
{
	// Rotate up and look vector about the right vector.

	XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);// 绕摄像机的右分量轴 转指定角度, 做1个旋转矩阵
	// 使用此旋转矩阵更新 摄像机的上分量和视线分量
	XMStoreFloat3(&mUp,   XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

	mViewDirty = true;
}

/// yaw旋转
void Camera::RotateY(float angle)
{
	// Rotate the basis vectors about the world y-axis.

	XMMATRIX R = XMMatrixRotationY(angle);// 注意,这里XMMatrixRotationY是 绕世界空间的上分量,而非摄像机自己的

	XMStoreFloat3(&mRight,   XMVector3TransformNormal(XMLoadFloat3(&mRight), R));
	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

	mViewDirty = true;
}

/// 构建观察矩阵
void Camera::UpdateViewMatrix()
{
	if(mViewDirty)// 当且仅当脏标记被命中的时候
	{
		XMVECTOR R = XMLoadFloat3(&mRight);
		XMVECTOR U = XMLoadFloat3(&mUp);
		XMVECTOR L = XMLoadFloat3(&mLook);
		XMVECTOR P = XMLoadFloat3(&mPosition);

		// 让摄像机的坐标向量彼此互相正交且保持单位长度; 重新正交规范化
		L = XMVector3Normalize(L);// 把观察方向规范化
		U = XMVector3Normalize(XMVector3Cross(L, R));

		// UL已经互为正交规范化向量,现在不需要对下列叉积再执行规范化
		R = XMVector3Cross(U, L);

		/* 已知从世界空间到观察空间的矩阵为
		* | Ux		Vx		Wx		0|
		* | Uy		Vy		Wy		0|
		* | Uz		Vz		Wz		0|
		* |-Q*U   -Q*V     -Q*W		1|
		其中U为right轴,V为up轴,W为look轴,Q是摄像机位置
		*/

		// 填写观察矩阵里的元素, 让位置和 相机三分量各自点积并取出 .x分量, 得到 x y z
		float x = -XMVectorGetX(XMVector3Dot(P, R));
		float y = -XMVectorGetX(XMVector3Dot(P, U));
		float z = -XMVectorGetX(XMVector3Dot(P, L));

		XMStoreFloat3(&mRight, R);
		XMStoreFloat3(&mUp, U);
		XMStoreFloat3(&mLook, L);

		mView(0, 0) = mRight.x;
		mView(1, 0) = mRight.y;
		mView(2, 0) = mRight.z;
		mView(3, 0) = x;

		mView(0, 1) = mUp.x;
		mView(1, 1) = mUp.y;
		mView(2, 1) = mUp.z;
		mView(3, 1) = y;

		mView(0, 2) = mLook.x;
		mView(1, 2) = mLook.y;
		mView(2, 2) = mLook.z;
		mView(3, 2) = z;

		mView(0, 3) = 0.0f;
		mView(1, 3) = 0.0f;
		mView(2, 3) = 0.0f;
		mView(3, 3) = 1.0f;

		mViewDirty = false;
	}
}


