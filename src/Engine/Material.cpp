#include "Engine/Material.h"
#include "Engine/Engine.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Paths.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Core/Misc/File.h"
#include "Core/Misc/ModuleContext.h"
#include <cmath>

namespace FP
{
	bool ValidateMaterial(const MaterialData& data)
	{
		if (data.Shader != "Lit" && data.Shader != "Unlit" && data.Shader != "Glass") return false;
		for (const FieldInfo& field : Material::GetFields())
		{
			const uint8* pointer = reinterpret_cast<const uint8*>(&data) + field.Offset;
			const int count = field.Type == FieldType::Color ? 4 : field.Type == FieldType::Vec2 ? 2 : field.Type == FieldType::Float ? 1 : 0;
			for (int i = 0; i < count; ++i)
				if (!std::isfinite(reinterpret_cast<const float*>(pointer)[i])) return false;
		}
		return data.Metallic >= 0 && data.Metallic <= 1 && data.Roughness >= 0 && data.Roughness <= 1 &&
			data.Specular >= 0 && data.Specular <= 1 && data.AlphaCutoff >= 0 && data.AlphaCutoff <= 1 &&
			data.EmissiveIntensity >= 0 && data.NormalStrength >= 0 && data.AmbientOcclusionStrength >= 0 &&
			data.Subsurface >= 0 && data.Subsurface <= 1 && data.IndexOfRefraction > 0 && data.Thickness >= 0 &&
			data.BaseColor.R >= 0 && data.BaseColor.R <= 1 && data.BaseColor.G >= 0 && data.BaseColor.G <= 1 && data.BaseColor.B >= 0 && data.BaseColor.B <= 1 && data.BaseColor.A >= 0 && data.BaseColor.A <= 1 &&
			data.EmissiveColor.R >= 0 && data.EmissiveColor.G >= 0 && data.EmissiveColor.B >= 0;
	}
	const Array<const char*>& Material::GetShaderNames()
	{
		static const Array<const char*> names = { "Lit", "Unlit", "Glass" };
		return names;
	}

	const Array<FieldInfo>& Material::GetFields()
	{
		static const Array<FieldInfo> fields = []() {
			Array<FieldInfo> list;
			FieldListBuilder<MaterialData>(list)
				.Field("Shader", &MaterialData::Shader).Choices(GetShaderNames())
				.Field("CelShaded", &MaterialData::CelShaded).Tooltip("Banded lighting and dark silhouette edges; unlit materials use banded color")
				.Field("BaseColor", &MaterialData::BaseColor)
				.Field("BaseColorMap", &MaterialData::BaseColorMap).AssetPath(Engine::ImageFilter)
				.Field("Tiling", &MaterialData::Tiling)
				.Field("Offset", &MaterialData::Offset)
				.Field("NormalMap", &MaterialData::NormalMap).AssetPath(Engine::ImageFilter)
				.Field("NormalStrength", &MaterialData::NormalStrength)
				.Field("AmbientOcclusionMap", &MaterialData::AmbientOcclusionMap).AssetPath(Engine::ImageFilter)
				.Field("AmbientOcclusionStrength", &MaterialData::AmbientOcclusionStrength)
				.Field("MetallicMap", &MaterialData::MetallicMap).AssetPath(Engine::ImageFilter)
				.Field("Metallic", &MaterialData::Metallic)
				.Field("RoughnessMap", &MaterialData::RoughnessMap).AssetPath(Engine::ImageFilter)
				.Field("Roughness", &MaterialData::Roughness)
				.Field("SpecularMap", &MaterialData::SpecularMap).AssetPath(Engine::ImageFilter)
				.Field("Specular", &MaterialData::Specular)
				.Field("EmissiveMap", &MaterialData::EmissiveMap).AssetPath(Engine::ImageFilter)
				.Field("EmissiveColor", &MaterialData::EmissiveColor)
				.Field("EmissiveIntensity", &MaterialData::EmissiveIntensity)
				.Field("SubsurfaceMap", &MaterialData::SubsurfaceMap).AssetPath(Engine::ImageFilter)
				.Field("SubsurfaceColor", &MaterialData::SubsurfaceColor)
				.Field("Subsurface", &MaterialData::Subsurface)
				.Field("IndexOfRefraction", &MaterialData::IndexOfRefraction)
				.Field("Thickness", &MaterialData::Thickness)
				.Field("AlphaCutoff", &MaterialData::AlphaCutoff)
				.Field("SoundTransmission", &MaterialData::SoundTransmission).Range(0, 1).Tooltip("Sound level passing through this object: 0 blocks, 1 passes at full volume")
				.Field("SoundAbsorption", &MaterialData::SoundAbsorption).Range(0, 1).Tooltip("Reflected sound absorbed by the surface: 0 reflects fully, 1 absorbs fully")
				.Field("SoundDamping", &MaterialData::SoundDamping).Range(0, 1).Tooltip("High-frequency loss for transmitted sound and reflections: 0 is clear, 1 is heavily muffled")
				.Field("TwoSided", &MaterialData::TwoSided);
			return list;
		}();
		return fields;
	}

	bool Material::Load()
	{
		String text;
		const String fullPath = Paths::Content(m_Path);
		if (!File::ReadAllText(fullPath.CStr(), text))
			return false;
		return LoadFromString(text.View());
	}

	bool Material::LoadFromString(std::string_view text)
	{
		MaterialData candidate;
		const Array<FieldInfo>& fields = GetFields();
		const bool parsed = FieldSerializer::ForEachLine(text, [&](int32 lineNumber, const Array<String>& tokens) {
			const FieldInfo* match = nullptr;
			for (const FieldInfo& field : fields)
			{
				if (tokens[0] == field.Name)
					match = &field;
			}
			if (match)
				return FieldSerializer::Read(*match, &candidate, tokens);
			else
				FP_LOG_WARN("%s line %d: unknown field %s", m_Path.CStr(), lineNumber, tokens[0].CStr());
			return true;
		});
		if (!parsed || !ValidateMaterial(candidate)) return false;
		m_Data = std::move(candidate);
		m_Dirty = false;
		return true;
	}

	String Material::SaveToString() const
	{
		if (!ValidateMaterial(m_Data))
			return {};
		String out = "# FirstPrinciples material\n";
		for (const FieldInfo& field : GetFields())
			FieldSerializer::Write(out, field, &m_Data, "");
		return out;
	}

	bool Material::Save()
	{
		if (!ValidateMaterial(m_Data)) return false;
		const String out = SaveToString();
		const String fullPath = Paths::Content(m_Path);
		if (!File::WriteAllText(fullPath.CStr(), out.View()))
		{
			FP_LOG_ERROR("Could not save material %s", fullPath.CStr());
			return false;
		}
		m_Dirty = false;
		FP_LOG("Saved material %s", m_Path.CStr());
		return true;
	}

	MaterialLibrary& MaterialLibrary::Get()
	{
		static MaterialLibrary library;
		return ResolveShared(&ModuleContext::MaterialLibrary, library);
	}

	Material* MaterialLibrary::Load(const String& reference)
	{
		const String path = Paths::AssetReference(reference);
		if (path.IsEmpty())
			return nullptr;
		const uint64 revision = Paths::GetAssets().Fingerprint(path);
		const bool changed = !m_Revisions.Contains(path) || m_Revisions.FindRef(path) != revision;
		if (changed)
		{
			m_Revisions.Add(path, revision);
			m_Failed.Remove(path);
		}
		if (RefPtr<Material>* cached = m_Materials.Find(path))
		{
			if (changed && !(*cached)->IsDirty()) (*cached)->Load();
			return cached->Get();
		}
		if (m_Failed.Contains(path))
			return nullptr;

		RefPtr<Material> material = MakeRef<Material>(path);
		if (!material->Load())
		{
			FP_LOG_WARN("Could not load material %s", path.CStr());
			m_Failed.Add(path);
			return nullptr;
		}
		m_Materials.Add(path, material);
		return material.Get();
	}

	Material* MaterialLibrary::Create(const String& path)
	{
		RefPtr<Material> material = MakeRef<Material>(path);
		if (!material->Save())
			return nullptr;
		Paths::GetAssets().Refresh();
		const String key = Paths::AssetReference(path);
		material = MakeRef<Material>(key);
		m_Failed.Remove(key);
		m_Materials.Add(key, material);
		return material.Get();
	}

	int32 MaterialLibrary::SaveDirty()
	{
		int32 saved = 0;
		for (auto& pair : m_Materials)
		{
			if (pair.Value->IsDirty())
			{
				if (!pair.Value->Save()) return -1;
				++saved;
			}
		}
		return saved;
	}

	Array<Material*> MaterialLibrary::GetLoaded() const
	{
		Array<Material*> loaded;
		for (const auto& pair : m_Materials)
			loaded.Add(pair.Value.Get());
		return loaded;
	}

	void MaterialLibrary::Clear()
	{
		m_Materials.Clear();
		m_Revisions.Clear();
		m_Failed.Clear();
	}
}
