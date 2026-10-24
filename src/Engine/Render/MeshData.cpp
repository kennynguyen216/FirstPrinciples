#include "Engine/Render/MeshData.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"

#include <cstdlib>

namespace FP
{
	void MeshData::ComputeBounds()
	{
		if (Vertices.IsEmpty())
		{
			BoundsMin = BoundsMax = {};
			return;
		}
		BoundsMin = BoundsMax = Vertices[0].Position;
		for (const MeshVertex& v : Vertices)
		{
			BoundsMin = { std::fmin(BoundsMin.X, v.Position.X), std::fmin(BoundsMin.Y, v.Position.Y), std::fmin(BoundsMin.Z, v.Position.Z) };
			BoundsMax = { std::fmax(BoundsMax.X, v.Position.X), std::fmax(BoundsMax.Y, v.Position.Y), std::fmax(BoundsMax.Z, v.Position.Z) };
		}
	}

	void MeshData::ComputeTangents()
	{
		Array<Vec3> tangents;
		Array<Vec3> bitangents;
		tangents.Resize(Vertices.Num());
		bitangents.Resize(Vertices.Num());

		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			const uint32 ia = Indices[i];
			const uint32 ib = Indices[i + 1];
			const uint32 ic = Indices[i + 2];
			const MeshVertex& a = Vertices[static_cast<int32>(ia)];
			const MeshVertex& b = Vertices[static_cast<int32>(ib)];
			const MeshVertex& c = Vertices[static_cast<int32>(ic)];

			const Vec3 e1 = b.Position - a.Position;
			const Vec3 e2 = c.Position - a.Position;
			// UV v runs down the image, so flip it to get a bitangent that points up the image.
			const Vec2 d1{ b.UV.X - a.UV.X, a.UV.Y - b.UV.Y };
			const Vec2 d2{ c.UV.X - a.UV.X, a.UV.Y - c.UV.Y };
			const float det = d1.X * d2.Y - d2.X * d1.Y;
			if (std::fabs(det) < 1e-12f)
				continue;
			const float r = 1.0f / det;
			const Vec3 t = (e1 * d2.Y - e2 * d1.Y) * r;
			const Vec3 bt = (e2 * d1.X - e1 * d2.X) * r;
			for (uint32 index : { ia, ib, ic })
			{
				tangents[static_cast<int32>(index)] += t;
				bitangents[static_cast<int32>(index)] += bt;
			}
		}

		for (int32 i = 0; i < Vertices.Num(); ++i)
		{
			MeshVertex& v = Vertices[i];
			const Vec3 n = v.Normal;
			// Gram Schmidt so the tangent is perpendicular to the normal.
			Vec3 t = (tangents[i] - n * Vec3::Dot(n, tangents[i])).Normalized();
			if (t.LengthSquared() < 0.5f)
				t = std::fabs(n.X) < 0.9f ? Vec3::Cross({ 1, 0, 0 }, n).Normalized() : Vec3::Cross({ 0, 1, 0 }, n).Normalized();
			const float sign = Vec3::Dot(Vec3::Cross(n, t), bitangents[i]) < 0 ? -1.0f : 1.0f;
			v.Tangent = { t, sign };
		}
	}

	namespace Primitives
	{
		MeshData Cube()
		{
			MeshData mesh;
			// Each face as normal, right, up. Corners are built so faces wind counter clockwise from outside.
			const Vec3 faces[6][3] = {
				{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
				{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
				{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
				{ { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
				{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
				{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
			};
			for (const auto& face : faces)
			{
				const Vec3 n = face[0];
				const Vec3 r = face[1];
				const Vec3 u = face[2];
				const uint32 base = static_cast<uint32>(mesh.Vertices.Num());
				const Vec2 corners[4] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
				for (const Vec2& c : corners)
				{
					MeshVertex v;
					v.Position = (n + r * c.X + u * c.Y) * 0.5f;
					v.Normal = n;
					v.UV = { c.X * 0.5f + 0.5f, 0.5f - c.Y * 0.5f };
					mesh.Vertices.Add(v);
				}
				for (uint32 index : { 0u, 3u, 2u, 0u, 2u, 1u })
					mesh.Indices.Add(base + index);
			}
			mesh.ComputeTangents();
			mesh.ComputeBounds();
			return mesh;
		}

		MeshData Sphere(int32 segments, int32 rings)
		{
			MeshData mesh;
			for (int32 ring = 0; ring <= rings; ++ring)
			{
				const float v = static_cast<float>(ring) / static_cast<float>(rings);
				const float phi = v * Pi;
				for (int32 segment = 0; segment <= segments; ++segment)
				{
					const float u = static_cast<float>(segment) / static_cast<float>(segments);
					const float theta = u * 2.0f * Pi;
					MeshVertex vertex;
					vertex.Normal = { std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta) };
					vertex.Position = vertex.Normal * 0.5f;
					vertex.UV = { u, v };
					mesh.Vertices.Add(vertex);
				}
			}
			const uint32 stride = static_cast<uint32>(segments + 1);
			for (int32 ring = 0; ring < rings; ++ring)
			{
				for (int32 segment = 0; segment < segments; ++segment)
				{
					const uint32 a = static_cast<uint32>(ring) * stride + static_cast<uint32>(segment);
					const uint32 b = a + stride;
					for (uint32 index : { a, b, b + 1, a, b + 1, a + 1 })
						mesh.Indices.Add(index);
				}
			}
			mesh.ComputeTangents();
			mesh.ComputeBounds();
			return mesh;
		}

		MeshData Plane()
		{
			MeshData mesh;
			const Vec3 positions[4] = { { -0.5f, 0, -0.5f }, { 0.5f, 0, -0.5f }, { 0.5f, 0, 0.5f }, { -0.5f, 0, 0.5f } };
			const Vec2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			for (int i = 0; i < 4; ++i)
				mesh.Vertices.Add({ positions[i], { 0, 1, 0 }, uvs[i], {} });
			for (uint32 index : { 0u, 3u, 2u, 0u, 2u, 1u })
				mesh.Indices.Add(index);
			mesh.ComputeTangents();
			mesh.ComputeBounds();
			return mesh;
		}

		MeshData Quad()
		{
			MeshData mesh;
			const Vec3 positions[4] = { { -0.5f, 0.5f, 0 }, { 0.5f, 0.5f, 0 }, { 0.5f, -0.5f, 0 }, { -0.5f, -0.5f, 0 } };
			const Vec2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			for (int i = 0; i < 4; ++i)
				mesh.Vertices.Add({ positions[i], { 0, 0, 1 }, uvs[i], {} });
			for (uint32 index : { 0u, 3u, 2u, 0u, 2u, 1u })
				mesh.Indices.Add(index);
			mesh.ComputeTangents();
			mesh.ComputeBounds();
			return mesh;
		}

		const Array<const char*>& GetNames()
		{
			static const Array<const char*> names = { "Cube", "Sphere", "Plane", "Quad" };
			return names;
		}

		bool Create(std::string_view name, MeshData& out)
		{
			if (name == "Cube") out = Cube();
			else if (name == "Sphere") out = Sphere();
			else if (name == "Plane") out = Plane();
			else if (name == "Quad") out = Quad();
			else return false;
			out.Sections = { { "Default", 0, static_cast<uint32>(out.Indices.Num()) } };
			return true;
		}
	}

	namespace
	{
		struct ObjCorner
		{
			int32 Position;
			int32 UV;
			int32 Normal;
			bool operator==(const ObjCorner&) const = default;
		};

		uint32 GetTypeHash(const ObjCorner& c)
		{
			return HashCombine(HashCombine(MixHash(static_cast<uint32>(c.Position)), MixHash(static_cast<uint32>(c.UV))), MixHash(static_cast<uint32>(c.Normal)));
		}

		// OBJ indices are 1 based and negative ones count back from the end.
		int32 ResolveIndex(const char* text, int32 count)
		{
			const int32 value = std::atoi(text);
			if (value > 0)
				return value - 1;
			if (value < 0)
				return count + value;
			return -1;
		}
	}

	bool ParseObj(std::string_view text, MeshData& out)
	{
		out = MeshData();
		Array<Vec3> positions;
		Array<Vec2> uvs;
		Array<Vec3> normals;
		HashMap<ObjCorner, uint32> lookup;
		bool hasNormals = true;

		// Triangles per material name, joined at the end so each name is one run.
		Array<String> sectionNames = { "Default" };
		Array<Array<uint32>> sectionIndices;
		sectionIndices.Emplace();
		int32 section = 0;

		usize start = 0;
		while (start < text.size())
		{
			usize end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			const String line(text.substr(start, end - start));
			start = end + 1;

			Array<String> tokens = line.Trimmed().Split(' ');
			if (tokens.IsEmpty())
				continue;
			const String& kind = tokens[0];
			auto num = [&](int32 i) { return i < tokens.Num() ? std::strtof(tokens[i].CStr(), nullptr) : 0.0f; };

			if (kind == "v")
				positions.Add({ num(1), num(2), num(3) });
			else if (kind == "vt")
				uvs.Add({ num(1), 1.0f - num(2) }); // OBJ v points up, ours points down
			else if (kind == "vn")
				normals.Add(Vec3(num(1), num(2), num(3)).Normalized());
			else if (kind == "usemtl")
			{
				const String name = tokens.Num() > 1 ? tokens[1] : String("Default");
				section = sectionNames.Find(name);
				if (section < 0)
				{
					section = sectionNames.Num();
					sectionNames.Add(name);
					sectionIndices.Emplace();
				}
			}
			else if (kind == "f" && tokens.Num() >= 4)
			{
				Array<uint32> face;
				for (int32 i = 1; i < tokens.Num(); ++i)
				{
					Array<String> parts = tokens[i].Split('/', true);
					ObjCorner corner{ ResolveIndex(parts[0].CStr(), positions.Num()), -1, -1 };
					if (parts.Num() > 1 && !parts[1].IsEmpty())
						corner.UV = ResolveIndex(parts[1].CStr(), uvs.Num());
					if (parts.Num() > 2 && !parts[2].IsEmpty())
						corner.Normal = ResolveIndex(parts[2].CStr(), normals.Num());
					if (corner.Position < 0 || corner.Position >= positions.Num())
						return false;
					hasNormals &= corner.Normal >= 0 && corner.Normal < normals.Num();

					uint32 index = 0;
					if (const uint32* existing = lookup.Find(corner))
					{
						index = *existing;
					}
					else
					{
						MeshVertex vertex;
						vertex.Position = positions[corner.Position];
						if (corner.UV >= 0 && corner.UV < uvs.Num())
							vertex.UV = uvs[corner.UV];
						if (corner.Normal >= 0 && corner.Normal < normals.Num())
							vertex.Normal = normals[corner.Normal];
						index = static_cast<uint32>(out.Vertices.Num());
						out.Vertices.Add(vertex);
						lookup.Add(corner, index);
					}
					face.Add(index);
				}
				Array<uint32>& indices = sectionIndices[section];
				for (int32 i = 1; i + 1 < face.Num(); ++i)
				{
					indices.Add(face[0]);
					indices.Add(face[i]);
					indices.Add(face[i + 1]);
				}
			}
		}

		// Sections that never got a face are dropped, like an unused Default.
		for (int32 i = 0; i < sectionNames.Num(); ++i)
		{
			if (sectionIndices[i].IsEmpty())
				continue;
			out.Sections.Add({ sectionNames[i], static_cast<uint32>(out.Indices.Num()), static_cast<uint32>(sectionIndices[i].Num()) });
			for (uint32 index : sectionIndices[i])
				out.Indices.Add(index);
		}

		if (out.Indices.IsEmpty())
			return false;

		if (!hasNormals)
		{
			// Smooth normals weighted by face area.
			for (MeshVertex& v : out.Vertices)
				v.Normal = {};
			for (int32 i = 0; i + 2 < out.Indices.Num(); i += 3)
			{
				MeshVertex& a = out.Vertices[static_cast<int32>(out.Indices[i])];
				MeshVertex& b = out.Vertices[static_cast<int32>(out.Indices[i + 1])];
				MeshVertex& c = out.Vertices[static_cast<int32>(out.Indices[i + 2])];
				const Vec3 n = Vec3::Cross(b.Position - a.Position, c.Position - a.Position);
				a.Normal += n;
				b.Normal += n;
				c.Normal += n;
			}
			for (MeshVertex& v : out.Vertices)
				v.Normal = v.Normal.Normalized();
		}

		out.ComputeTangents();
		out.ComputeBounds();
		return true;
	}

	bool LoadMesh(std::string_view nameOrPath, MeshData& out)
	{
		if (Primitives::Create(nameOrPath, out))
			return true;
		String text;
		const String fullPath = Paths::Content(nameOrPath);
		return File::ReadAllText(fullPath.CStr(), text) && ParseObj(text.View(), out);
	}
}
