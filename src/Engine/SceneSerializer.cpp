#include "Engine/SceneSerializer.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/FieldSerializer.h"
#include "Core/Misc/File.h"

#include <cstdlib>
#include <cmath>

namespace FP::SceneSerializer
{
	namespace
	{
		// Version 2 made Transform 3D.
		constexpr int32 FormatVersion = 2;

		// Version 1 scenes stored a 2D transform. Rotation was one angle around Z and Scale had no Z.
		void MigrateV1Field(const ComponentInfo& info, const FieldInfo& field, Array<String>& tokens)
		{
			if (std::string_view(info.Name) != "Transform")
				return;
			if (std::string_view(field.Name) == "Rotation" && tokens.Num() == 2)
				tokens = { tokens[0], "0", "0", tokens[1] };
			else if (field.Type == FieldType::Vec3 && tokens.Num() == 3)
				tokens.Add(std::string_view(field.Name) == "Scale" ? "1" : "0");
		}
	}

	bool Validate(World& world)
	{
		auto color = [](const Color& c) { return std::isfinite(c.R) && std::isfinite(c.G) && std::isfinite(c.B) && std::isfinite(c.A) && c.R >= 0 && c.G >= 0 && c.B >= 0 && c.A >= 0 && c.A <= 1; };
		for (Entity entity : world.GetEntities())
		{
			for (const ComponentInfo* info : ComponentRegistry::Get().GetAll())
			{
				void* component = info->TryGet(world.GetRegistry(), entity);
				if (!component) continue;
				for (const FieldInfo& field : info->Fields)
				{
					const int count = field.Type == FieldType::Float ? 1 : field.Type == FieldType::Vec2 ? 2 : field.Type == FieldType::Vec3 ? 3 : field.Type == FieldType::Color ? 4 : 0;
					const float* values = reinterpret_cast<const float*>(static_cast<const uint8*>(component) + field.Offset);
					for (int i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
				}
			}
			if (const auto* t = world.TryGet<TransformComponent>(entity); t && (t->Scale.X == 0 || t->Scale.Y == 0 || t->Scale.Z == 0)) return false;
			if (const auto* l = world.TryGet<PointLightComponent>(entity); l && (!color(l->Color) || l->Intensity < 0 || l->Range <= 0)) return false;
			if (const auto* l = world.TryGet<DirectionalLightComponent>(entity); l && (!color(l->Color) || l->Intensity < 0)) return false;
			if (const auto* l = world.TryGet<AmbientLightComponent>(entity); l && (!color(l->Color) || l->Intensity < 0)) return false;
			if (const auto* c = world.TryGet<CameraComponent>(entity); c && (c->Zoom <= 0 || c->FieldOfView <= 0 || c->FieldOfView >= 180 || c->NearClip <= 0 || c->FarClip <= c->NearClip || c->Exposure < 0 || !color(c->Background))) return false;
			if (const auto* s = world.TryGet<SpriteComponent>(entity); s && (!color(s->Tint) || s->Size.X <= 0 || s->Size.Y <= 0)) return false;
		}
		return true;
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

	static bool ParseScene(World& world, std::string_view text)
	{
		entt::registry& registry = world.GetRegistry();

		Entity entity = NullEntity;
		const ComponentInfo* info = nullptr;
		void* component = nullptr;
		int32 version = 1;
		bool ok = true;

		const bool parsed = FieldSerializer::ForEachLine(text, [&](int32 lineNumber, const Array<String>& tokens) {
			const String& keyword = tokens[0];
			if (keyword == "version")
			{
				int64 parsed = 1;
				if (tokens.Num() != 2 || !tokens[1].ToInt(parsed) || parsed < 1 || parsed > FormatVersion || entity != NullEntity)
					return false;
				version = static_cast<int32>(parsed);
				return true;
			}
			if (keyword == "entity")
			{
				Guid id;
				if (tokens.Num() != 2 || !Guid::Parse(tokens[1], id) || !id.IsValid() || world.FindEntityByGuid(id) != NullEntity)
					return false;
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
				Array<String> values = tokens;
				if (version < 2)
					MigrateV1Field(*info, *field, values);
				if (!FieldSerializer::Read(*field, component, values))
				{
					FP_LOG_ERROR("Scene line %d: invalid %s", lineNumber, field->Name);
					return false;
				}
			}
			else
			{
				FP_LOG_WARN("Scene line %d: %s has no field %s", lineNumber, info->Name, keyword.CStr());
			}
			return true;
		});
		return ok && parsed;
	}

	bool LoadFromString(World& world, std::string_view text)
	{
		StrongObjectPtr<World> candidate(NewObject<World>());
		if (!ParseScene(*candidate.Get(), text) || !Validate(*candidate.Get()))
			return false;
		world.ReplaceScene(*candidate.Get());
		return true;
	}

	bool SaveToFile(World& world, const char* path)
	{
		if (!Validate(world)) { FP_LOG_ERROR("Refusing to save invalid scene %s", path); return false; }
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
