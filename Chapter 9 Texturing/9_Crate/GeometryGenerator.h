//***************************************************************************************
// GeometryGenerator.h by Frank Luna (C) 2011 All Rights Reserved.
//   
// Defines a static class for procedurally generating the geometry of 
// common mathematical objects.
//
// All triangles are generated "outward" facing.  If you want "inward" 
// facing triangles (for example, if you want to place the camera inside
// a sphere to simulate a sky), you will need to:
//   1. Change the Direct3D cull mode or manually reverse the winding order.
//   2. Invert the normal.
//   3. Update the texture coordinates and tangent vectors.
//***************************************************************************************

#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <vector>

/* 用户提供参数以自动生成的几何体存入GeometryGenerator类里
* 它是一个工具类,用于生成栅格,球体,柱体,长方体
* 此类还可以创建出后续技术要使用的顶点数据,然后存到顶点缓存里*/
class GeometryGenerator
{
public:

	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	/* GeometryGenerator::Vertex型 结构体, 带有Postion, Normal, Tangent, uv等构造器*/
	struct Vertex
	{
		Vertex() {}
		Vertex(
			const DirectX::XMFLOAT3& p,
			const DirectX::XMFLOAT3& n,
			const DirectX::XMFLOAT3& t,
			const DirectX::XMFLOAT2& uv) :
			Position(p),
			Normal(n),
			TangentU(t),
			TexC(uv) {}
		Vertex(
			float px, float py, float pz,
			float nx, float ny, float nz,
			float tx, float ty, float tz,
			float u, float v) :
			Position(px, py, pz),
			Normal(nx, ny, nz),
			TangentU(tx, ty, tz),
			TexC(u, v) {}

		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT3 Normal;
		DirectX::XMFLOAT3 TangentU;
		DirectX::XMFLOAT2 TexC;
	};

	/* MeshData是一个嵌套在GeometryGenerator类里用于存储顶点\索引表的简易结构体
	* MeshData结构体存有顶点数组\索引数组
	* 此结构体通常被拿来构造 模型几何体
	*/
	struct MeshData
	{
		std::vector<Vertex> Vertices;// MeshData结构体里的 顶点数组
		std::vector<uint32> Indices32;// MeshData结构体里的 索引数组

		/* 构建1个uint16型的索引数组*/
		std::vector<uint16>& GetIndices16()
		{
			if (mIndices16.empty()) 			{
				mIndices16.resize(Indices32.size());
				for (size_t i = 0; i < Indices32.size(); ++i)
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
			}

			return mIndices16;
		}

	private:
		std::vector<uint16> mIndices16;
	};

	///<summary>
	/// Creates a box centered at the origin with the given dimensions, where each
	/// face has m rows and n columns of vertices.
	///</summary>
	/// 用于生成立方体的纹理坐标
	MeshData CreateBox(float width, float height, float depth, uint32 numSubdivisions);

	///<summary>
	/// Creates a sphere centered at the origin with the given radius.  The
	/// slices and stacks parameters control the degree of tessellation.
	///</summary>
	MeshData CreateSphere(float radius, uint32 sliceCount, uint32 stackCount);

	///<summary>
	/// Creates a geosphere centered at the origin with the given radius.  The
	/// depth controls the level of tessellation.
	///</summary>
	MeshData CreateGeosphere(float radius, uint32 numSubdivisions);


	/// 生成圆台的基本思路是遍历每个环, 并生成位于环上的各个顶点
	MeshData CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount,/*切片数量*/ uint32 stackCount/*层数*/);

	
	/// 创建栅格,用来绘制水波荡漾
	/// 待栅格创建后,可以从MeshData里获取所需顶点, 根据顶点的高度(即y坐标)把平坦的栅格变为表现山峰起伏的曲面
	MeshData CreateGrid(float width, float depth, uint32 m, uint32 n);

	///<summary>
	/// Creates a quad aligned with the screen.  This is useful for postprocessing and screen effects.
	///</summary>
	MeshData CreateQuad(float x, float y, float w, float h, float depth);

private:
	void Subdivide(MeshData& meshData);
	Vertex MidPoint(const Vertex& v0, const Vertex& v1);
	void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
	void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
};

