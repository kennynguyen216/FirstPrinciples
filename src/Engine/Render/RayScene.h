#pragma once

#include "Engine/Render/MeshData.h"
#include "Engine/Render/RenderView.h"

namespace FP
{
	struct RayBounds
	{
		Vec3 Min;
		Vec3 Max;
		bool Intersect(const Ray& ray, float minimum, float maximum, float* exit = nullptr) const;
	};

	struct RayNode
	{
		Vec4 Min;
		Vec4 Max;
		uint32 First = 0;
		uint32 Count = 0;
		uint32 Right = 0;
		uint32 Escape = 0;
	};

	struct RayTriangle
	{
		Vec4 Position[3];
		Vec4 Normal[3];
		Vec4 Tangent[3];
		Vec4 UV01;
		Vec4 UV2;
		uint32 Primitive = 0;
		uint32 Padding[3] = {};
	};

	class RayMesh : public RefCounted
	{
	public:
		static RefPtr<RayMesh> Build(const MeshData& data, uint32 firstIndex = 0, uint32 indexCount = UINT32_MAX);
		Array<RayNode> Nodes;
		Array<RayTriangle> Triangles;
		RayBounds Bounds;
		uint32 SourceTriangleCount = 0;
	};

	struct RayIdentity
	{
		uint64 Scene = 0;
		uint32 Entity = 0;
		uint32 Geometry = 0;
		bool operator==(const RayIdentity&) const = default;
	};

	struct RayInstance
	{
		RayIdentity Identity;
		RefPtr<RayMesh> Mesh;
		Mat4 Transform;
		Mat4 Inverse;
		RayBounds Bounds;
		uint32 Material = 0;
		uint32 CachePage = 0;
		uint32 CaptureGrid = 4;
		uint32 CaptureSides = 1;
	};

	enum class RayResultClass : uint32
	{
		Surface,
		UncoveredSurface,
		Escaped,
		ClearSegment,
		UnknownCoverage,
		Exhausted,
		Invalid,
	};

	struct RayHit
	{
		RayResultClass Class = RayResultClass::Invalid;
		RayIdentity Identity;
		uint32 Instance = 0;
		uint32 Triangle = 0;
		uint32 Primitive = 0;
		float Distance = 0;
		Vec3 Barycentrics;
		Vec3 Position;
		Vec3 GeometricNormal;
		Vec3 ShadingNormal;
		Vec2 UV;
		uint32 Visits = 0;
	};

	class RayScene
	{
	public:
		static constexpr uint32 MaxInstances = 256;
		static constexpr uint32 MaxTriangles = 131072;
		static constexpr uint32 MaxVisits = 8192;

		bool Publish(Array<RayInstance> instances, bool complete = true);
		RayHit Trace(const Ray& ray, float maximum = 128, bool segment = false, uint32 budget = MaxVisits) const;
		bool IsVisible(Vec3 from, Vec3 to, Vec3 geometricNormal) const;
		const Array<RayInstance>& GetInstances() const { return m_Instances; }
		const Array<RayNode>& GetNodes() const { return m_Nodes; }
		uint64 GetRevision() const { return m_Revision; }
		bool IsComplete() const { return m_Complete; }
		usize GetMemoryBytes() const;

	private:
		Array<RayInstance> m_Instances;
		Array<RayNode> m_Nodes;
		bool m_Complete = true;
		uint64 m_Revision = 0;
	};

	bool IsFinite(Vec3 value);
	constexpr float TransportUnitSample(uint32 bits) { return (static_cast<float>(bits >> 9) + 0.5f) / 8388608.0f; }
	bool IsFinite(const Mat4& value);
	float PunctualAttenuation(float distance, float range);
	float SpotAttenuation(float cosine, float innerDegrees, float outerDegrees);
	uint64 MeshFingerprint(const MeshData& mesh);
}
