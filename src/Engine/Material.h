#pragma once

#include "Engine/ComponentRegistry.h"

namespace FP
{
	// Visual and acoustic properties saved as a .material text file.
	// Every map is optional. Scalars multiply their map, so a missing map means "use the scalar".
	struct MaterialData
	{
		String Shader = "Lit"; // Lit, Unlit or Glass

		Color BaseColor;
		String BaseColorMap;
		Vec2 Tiling{ 1, 1 };
		Vec2 Offset;

		String NormalMap;
		float NormalStrength = 1;

		String AmbientOcclusionMap;
		float AmbientOcclusionStrength = 1;

		String MetallicMap;
		float Metallic = 0;

		String RoughnessMap;
		float Roughness = 0.5f;

		String SpecularMap;
		float Specular = 0.5f;

		String EmissiveMap;
		Color EmissiveColor{ 0, 0, 0, 1 };
		float EmissiveIntensity = 1;

		String SubsurfaceMap; // white where light scatters through
		Color SubsurfaceColor{ 1, 0.4f, 0.3f, 1 };
		float Subsurface = 0;

		// Glass only. Index of refraction and how far the background bends.
		float IndexOfRefraction = 1.5f;
		float Thickness = 0.5f;

		float AlphaCutoff = 0.01f;

		float SoundTransmission = 0.25f;
		float SoundAbsorption = 0.4f;
		float SoundDamping = 0.5f;
	};

	// A shared material asset. Entities point at it by path through MaterialComponent.
	class Material : public RefCounted
	{
	public:
		explicit Material(String path) : m_Path(std::move(path)) {}

		const String& GetPath() const { return m_Path; }
		MaterialData& GetData() { return m_Data; }
		const MaterialData& GetData() const { return m_Data; }

		bool IsDirty() const { return m_Dirty; }
		void MarkDirty() { m_Dirty = true; }

		bool Load();
		bool Save();

		// The .material file text. Undo keeps these to put edits back.
		String SaveToString() const;
		void LoadFromString(std::string_view text);

		static const Array<FieldInfo>& GetFields();
		static const Array<const char*>& GetShaderNames();

	private:
		String m_Path;
		MaterialData m_Data;
		bool m_Dirty = false;
	};

	// Loads each .material once and keeps it for everyone who uses the same path.
	class MaterialLibrary
	{
	public:
		static MaterialLibrary& Get();

		// Path is relative to Content. Returns null for an empty path or a missing file (logged once).
		Material* Load(const String& path);

		// Writes a new material with default values and returns it.
		Material* Create(const String& path);

		// Saves every material changed in the editor. Returns how many were written.
		int32 SaveDirty();

		// Every material loaded so far.
		Array<Material*> GetLoaded() const;

		void Clear();

	private:
		HashMap<String, RefPtr<Material>> m_Materials;
		Array<String> m_Failed;
	};
}
