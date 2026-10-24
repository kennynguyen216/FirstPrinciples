#include "Engine/SceneSerializer.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/FieldSerializer.h"
#include "Core/Misc/File.h"

#include <cstdlib>

namespace FP::SceneSerializer
{
	namespace
	{
		// Version 2 made Transform 3D.
		constexpr int32 FormatVersion = 2;

		// Version 1 scenes stored a 2D transform. Rotation was one angle around Z and Scale had no Z.
		void MigrateV1Field(const ComponentInfo& info, const FieldInfo& field, void* component, const Array<String>& tokens)
		{
			if (std::string_view(info.Name) != "Transform")
				return;
			Vec3& value = field.Get<Vec3>(component);
			if (std::string_view(field.Name) == "Rotation" && tokens.Num() == 2)
				value = { 0, 0, std::strtof(tokens[1].CStr(), nullptr) };
			else if (std::string_view(field.Name) == "Scale" && tokens.Num() == 3)
				value.Z = 1;
		}
	}

	String SaveToString(World& world)
	{
		String out;
		out.Appendf("# FirstPrinciples scene\nversion %d\n", FormatVersion);

		const Array<ComponentInfo*>& components = ComponentRegistry::Get().GetAll();
		entt::registry& registry = world.GetRegistry();
		for (Entity entity : world.GetEntities())
		{
			out.Appendf("\nentity %s\n", world.Get<IdComponent>(entity).Id.ToString().CStr());
			for (const ComponentInfo* info : components)
			{
				void* component = info->TryGet(registry, entity);
				if (!component || info->TypeId == entt::type_hash<IdComponent>::value())
					continue;
				out.Appendf("component %s\n", info->Name);
				for (const FieldInfo& field : info->Fields)
					FieldSerializer::Write(out, field, component);
			}
		}
		return out;
	}

	bool LoadFromString(World& world, std::string_view text)
	{
		world.Clear();
		entt::registry& registry = world.GetRegistry();

		Entity entity = NullEntity;
		const ComponentInfo* info = nullptr;
		void* component = nullptr;
		int32 version = 1;
		bool ok = true;

		FieldSerializer::ForEachLine(text, [&](int32 lineNumber, const Array<String>& tokens) {
			const String& keyword = tokens[0];
			if (keyword == "version")
			{
				int64 parsed = 1;
				if (tokens.Num() > 1 && tokens[1].ToInt(parsed))
					version = static_cast<int32>(parsed);
				return true;
			}
			if (keyword == "entity")
			{
				Guid id;
				if (tokens.Num() < 2 || !Guid::Parse(tokens[1], id))
					id = Guid::New();
				entity = world.CreateEmptyEntity(id);
				info = nullptr;
				component = nullptr;
				return true;
			}
			if (entity == NullEntity)
			{
				FP_LOG_ERROR("Scene line %d: expected an entity first", lineNumber);
				ok = false;
				return false;
			}
			if (keyword == "component")
			{
				info = tokens.Num() > 1 ? ComponentRegistry::Get().Find(tokens[1]) : nullptr;
				component = info ? info->Add(registry, entity) : nullptr;
				if (!info)
					FP_LOG_WARN("Scene line %d: unknown component %s", lineNumber, tokens.Num() > 1 ? tokens[1].CStr() : "");
				return true;
			}

			// Anything else is a field of the current component.
			if (!component)
				return true;
			if (const FieldInfo* field = info->FindField(keyword))
			{
				FieldSerializer::Read(*field, component, tokens);
				if (version < 2)
					MigrateV1Field(*info, *field, component, tokens);
			}
			else
			{
				FP_LOG_WARN("Scene line %d: %s has no field %s", lineNumber, info->Name, keyword.CStr());
			}
			return true;
		});
		return ok;
	}

	bool SaveToFile(World& world, const char* path)
	{
		if (!File::WriteAllText(path, SaveToString(world).View()))
		{
			FP_LOG_ERROR("Could not write scene %s", path);
			return false;
		}
		FP_LOG("Saved scene %s", path);
		return true;
	}

	bool LoadFromFile(World& world, const char* path)
	{
		String text;
		if (!File::ReadAllText(path, text))
		{
			FP_LOG_ERROR("Could not read scene %s", path);
			return false;
		}
		return LoadFromString(world, text.View());
	}
}
