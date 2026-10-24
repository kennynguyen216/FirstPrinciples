#include "Engine/SceneSerializer.h"
#include "Engine/ComponentRegistry.h"
#include "Core/Misc/File.h"

#include <cstdlib>

namespace FP::SceneSerializer
{
	namespace
	{
		constexpr int32 FormatVersion = 1;

		void WriteQuoted(String& out, const String& text)
		{
			out += '"';
			for (char c : text)
			{
				if (c == '"' || c == '\\')
					out += '\\';
				if (c == '\n')
				{
					out += "\\n";
					continue;
				}
				out += c;
			}
			out += '"';
		}

		void WriteField(String& out, const FieldInfo& field, void* component)
		{
			out.Appendf("  %s ", field.Name);
			switch (field.Type)
			{
				case FieldType::Bool: out += field.Get<bool>(component) ? "true" : "false"; break;
				case FieldType::Int: out.Appendf("%d", field.Get<int32>(component)); break;
				case FieldType::Float: out.Appendf("%.9g", field.Get<float>(component)); break;
				case FieldType::Vec2:
				{
					const Vec2& v = field.Get<Vec2>(component);
					out.Appendf("%.9g %.9g", v.X, v.Y);
					break;
				}
				case FieldType::Color:
				{
					const Color& c = field.Get<Color>(component);
					out.Appendf("%.9g %.9g %.9g %.9g", c.R, c.G, c.B, c.A);
					break;
				}
				case FieldType::String: WriteQuoted(out, field.Get<String>(component)); break;
			}
			out += '\n';
		}

		// Splits on spaces and keeps quoted strings whole.
		Array<String> Tokenize(std::string_view line)
		{
			Array<String> tokens;
			usize i = 0;
			while (i < line.size())
			{
				while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
					++i;
				if (i >= line.size())
					break;
				String token;
				if (line[i] == '"')
				{
					++i;
					while (i < line.size() && line[i] != '"')
					{
						if (line[i] == '\\' && i + 1 < line.size())
						{
							++i;
							token += line[i] == 'n' ? '\n' : line[i];
						}
						else
						{
							token += line[i];
						}
						++i;
					}
					++i;
				}
				else
				{
					while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
						token += line[i++];
				}
				tokens.Add(std::move(token));
			}
			return tokens;
		}

		float ParseFloat(const Array<String>& tokens, int32 index)
		{
			return index < tokens.Num() ? std::strtof(tokens[index].CStr(), nullptr) : 0.0f;
		}

		void ReadField(const FieldInfo& field, void* component, const Array<String>& tokens)
		{
			switch (field.Type)
			{
				case FieldType::Bool: field.Get<bool>(component) = tokens.Num() > 1 && tokens[1] == "true"; break;
				case FieldType::Int: field.Get<int32>(component) = tokens.Num() > 1 ? std::atoi(tokens[1].CStr()) : 0; break;
				case FieldType::Float: field.Get<float>(component) = ParseFloat(tokens, 1); break;
				case FieldType::Vec2: field.Get<Vec2>(component) = { ParseFloat(tokens, 1), ParseFloat(tokens, 2) }; break;
				case FieldType::Color:
					field.Get<Color>(component) = { ParseFloat(tokens, 1), ParseFloat(tokens, 2), ParseFloat(tokens, 3), ParseFloat(tokens, 4) };
					break;
				case FieldType::String: field.Get<String>(component) = tokens.Num() > 1 ? tokens[1] : String(); break;
			}
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
					WriteField(out, field, component);
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
		int32 lineNumber = 0;

		usize start = 0;
		while (start <= text.size())
		{
			usize end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			const std::string_view line = text.substr(start, end - start);
			start = end + 1;
			++lineNumber;

			Array<String> tokens = Tokenize(line);
			if (tokens.IsEmpty() || tokens[0].StartsWith("#"))
				continue;

			const String& keyword = tokens[0];
			if (keyword == "version")
			{
				continue;
			}
			if (keyword == "entity")
			{
				Guid id;
				if (tokens.Num() < 2 || !Guid::Parse(tokens[1], id))
					id = Guid::New();
				entity = world.CreateEmptyEntity(id);
				info = nullptr;
				component = nullptr;
				continue;
			}
			if (entity == NullEntity)
			{
				FP_LOG_ERROR("Scene line %d: expected an entity first", lineNumber);
				return false;
			}
			if (keyword == "component")
			{
				info = tokens.Num() > 1 ? ComponentRegistry::Get().Find(tokens[1]) : nullptr;
				component = info ? info->Add(registry, entity) : nullptr;
				if (!info)
					FP_LOG_WARN("Scene line %d: unknown component %s", lineNumber, tokens.Num() > 1 ? tokens[1].CStr() : "");
				continue;
			}

			// Anything else is a field of the current component.
			if (!component)
				continue;
			if (const FieldInfo* field = info->FindField(keyword))
				ReadField(*field, component, tokens);
			else
				FP_LOG_WARN("Scene line %d: %s has no field %s", lineNumber, info->Name, keyword.CStr());
		}
		return true;
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
