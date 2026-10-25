#include "Engine/Material.h"
#include "Engine/Engine.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Paths.h"
#include "Core/Misc/File.h"
#include "Core/Misc/ModuleContext.h"

namespace FP
{
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
				.Field("SoundDamping", &MaterialData::SoundDamping).Range(0, 1).Tooltip("High-frequency loss for transmitted sound and reflections: 0 is clear, 1 is heavily muffled");
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
		LoadFromString(text.View());
		m_Dirty = false;
		return true;
	}

	void Material::LoadFromString(std::string_view text)
	{
		m_Data = MaterialData();
		const Array<FieldInfo>& fields = GetFields();
		FieldSerializer::ForEachLine(text, [&](int32 lineNumber, const Array<String>& tokens) {
			const FieldInfo* match = nullptr;
			for (const FieldInfo& field : fields)
			{
				if (tokens[0] == field.Name)
					match = &field;
			}
			if (match)
				FieldSerializer::Read(*match, &m_Data, tokens);
			else
				FP_LOG_WARN("%s line %d: unknown field %s", m_Path.CStr(), lineNumber, tokens[0].CStr());
			return true;
		});
	}

	String Material::SaveToString() const
	{
		String out = "# FirstPrinciples material\n";
		for (const FieldInfo& field : GetFields())
			FieldSerializer::Write(out, field, &m_Data, "");
		return out;
	}

	bool Material::Save()
	{
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

	Material* MaterialLibrary::Load(const String& path)
	{
		if (path.IsEmpty())
			return nullptr;
		if (RefPtr<Material>* cached = m_Materials.Find(path))
			return cached->Get();
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
		m_Failed.Remove(path);
		m_Materials.Add(path, material);
		return material.Get();
	}

	int32 MaterialLibrary::SaveDirty()
	{
		int32 saved = 0;
		for (auto& pair : m_Materials)
		{
			if (pair.Value->IsDirty() && pair.Value->Save())
				++saved;
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
		m_Failed.Clear();
	}
}
