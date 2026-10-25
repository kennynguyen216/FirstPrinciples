#include "Engine/Assets/AssetDatabase.h"
#include "Engine/ComponentRegistry.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Json.h"
#include "Engine/Scripting/ScriptRuntime.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace FP
{
	namespace
	{
		namespace fs = std::filesystem;

		fs::path Native(std::string_view path)
		{
			return fs::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
		}

		String Text(const fs::path& path)
		{
			const auto text = path.generic_u8string();
			return String(reinterpret_cast<const char*>(text.data()), static_cast<int32>(text.size()));
		}

		String Normalize(std::string_view path)
		{
			if (path.empty() || path.find('\\') != path.npos || path.find(':') != path.npos || path.find('\0') != path.npos)
				return {};
			const fs::path native = Native(path).lexically_normal();
			if (native.is_absolute() || native.empty() || *native.begin() == ".." || native == ".")
				return {};
			return Text(native);
		}

		bool ReadText(const fs::path& path, String& text)
		{
			std::error_code ec;
			const auto size = fs::file_size(path, ec);
			if (ec || size > 32 * 1024 * 1024)
				return false;
			std::ifstream file(path, std::ios::binary);
			std::string bytes(static_cast<size_t>(size), '\0');
			if (!file || !file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
				return false;
			text = bytes;
			return true;
		}

		bool AtomicWrite(const fs::path& path, std::string_view text)
		{
			fs::path temporary = path;
			temporary += Native("." + std::string(Guid::New().ToString().View()) + ".tmp");
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			file.flush();
			const bool written = file.good();
			file.close();
			std::error_code ec;
			bool replaced = false;
			if (written && !file.fail())
			{
#if defined(_WIN32)
				replaced = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
				fs::rename(temporary, path, ec);
				replaced = !ec;
#endif
			}
			if (!replaced)
				fs::remove(temporary, ec);
			return replaced;
		}

		uint64 Hash(std::string_view bytes, uint64 seed = 14695981039346656037ull)
		{
			for (unsigned char byte : bytes)
				seed = (seed ^ byte) * 1099511628211ull;
			return seed;
		}

		uint64 FileHash(const fs::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return 0;
			uint64 hash = 14695981039346656037ull;
			char bytes[65536];
			while (file.read(bytes, sizeof(bytes)) || file.gcount())
				hash = Hash(std::string_view(bytes, static_cast<size_t>(file.gcount())), hash);
			return file.bad() ? 0 : hash;
		}

		bool Inside(std::string_view path, std::string_view folder)
		{
			return path == folder || (path.starts_with(folder) && path.size() > folder.size() && path[folder.size()] == '/');
		}

		bool NativeReferences(std::string_view path)
		{
			return path.ends_with(".scene") || path.ends_with(".material");
		}
	}

	bool AssetDatabase::IsReference(std::string_view text)
	{
		return text.starts_with("asset://");
	}

	bool AssetDatabase::IsTexture(std::string_view path)
	{
		const String extension = Text(Native(path).extension()).ToLower();
		return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
	}

	bool AssetDatabase::Open(const String& contentRoot, bool writable)
	{
		m_Root = contentRoot;
		m_Writable = writable;
		m_Assets.clear();
		m_Ids.clear();
		m_Aliases.clear();
		m_Fingerprints.clear();
		return Refresh();
	}

	bool AssetDatabase::ReadMetadata(AssetRecord& asset)
	{
		String text;
		if (!ReadText(Native(m_Root) / Native(asset.Path + MetadataExtension), text))
			return false;
		bool version = false;
		bool id = false;
		bool mipmaps = false;
		bool color = false;
		for (const String& line : text.Split('\n'))
		{
			if (line.Trimmed().IsEmpty() || line.Trimmed().StartsWith("#")) continue;
			bool valid = false;
			const auto tokens = FieldSerializer::Tokenize(line, &valid);
			if (!valid || tokens.Num() != 2) return false;
			if (tokens[0] == "version" && !version)
			{
				if (tokens[1] != "1") return false;
				version = true;
			}
			else if (tokens[0] == "id" && !id)
			{
				if (!Guid::Parse(tokens[1], asset.Id) || !asset.Id.IsValid()) return false;
				id = true;
			}
			else if (tokens[0] == "mipmaps" && !mipmaps)
			{
				if (tokens[1] != "true" && tokens[1] != "false") return false;
				asset.Settings.GenerateMipmaps = tokens[1] == "true";
				mipmaps = true;
			}
			else if (tokens[0] == "colorSpace" && !color)
			{
				if (tokens[1] != "auto" && tokens[1] != "srgb" && tokens[1] != "linear") return false;
				asset.Settings.ColorSpace = tokens[1] == "auto" ? AssetColorSpace::Auto : tokens[1] == "srgb" ? AssetColorSpace::SRGB : AssetColorSpace::Linear;
				color = true;
			}
			else return false;
		}
		return version && id && mipmaps && color;
	}

	bool AssetDatabase::SaveMetadata(const AssetRecord& asset)
	{
		const char* color = asset.Settings.ColorSpace == AssetColorSpace::Auto ? "auto" : asset.Settings.ColorSpace == AssetColorSpace::SRGB ? "srgb" : "linear";
		const String text = String::Format("version 1\nid %s\nmipmaps %s\ncolorSpace %s\n", asset.Id.ToString().CStr(), asset.Settings.GenerateMipmaps ? "true" : "false", color);
		return AtomicWrite(Native(m_Root) / Native(asset.Path + MetadataExtension), text);
	}

	bool AssetDatabase::Refresh()
	{
		m_Issues.Clear();
		std::map<std::string, AssetRecord, std::less<>> assets;
		std::map<std::string, std::string, std::less<>> ids;
		std::set<std::string> folded;
		std::set<std::string> duplicates;
		std::error_code ec;
		fs::recursive_directory_iterator it(Native(m_Root), ec), end;
		if (ec)
		{
			m_Issues.Add({ m_Root, "Cannot scan Content" });
			return false;
		}
		for (; it != end; it.increment(ec))
		{
			if (ec) break;
			const fs::path relative = it->path().lexically_relative(Native(m_Root));
			const String path = Text(relative);
			if (it->is_symlink(ec))
			{
				it.disable_recursion_pending();
				m_Issues.Add({ path, "Asset symlinks are not supported" });
				continue;
			}
			if (it->is_directory(ec)) continue;
			if (path.EndsWith(MetadataExtension) || path.EndsWith(".tmp") || path.StartsWith(".") || path.Contains("/.")) continue;
			if (!it->is_regular_file(ec)) continue;
			if (Normalize(path).IsEmpty())
			{
				m_Issues.Add({ path, "Invalid asset path" });
				continue;
			}
			if (!folded.insert(std::string(path.ToLower().View())).second)
				m_Issues.Add({ path, "Asset paths differ only by case" });
			AssetRecord asset;
			asset.Path = path;
			const bool hasMetadata = fs::exists(Native(m_Root) / Native(path + MetadataExtension), ec);
			if (hasMetadata && !ReadMetadata(asset))
			{
				m_Issues.Add({ path, "Invalid asset metadata" });
				continue;
			}
			if (!hasMetadata && m_Writable)
			{
				asset.Id = Guid::New();
				if (!SaveMetadata(asset))
				{
					m_Issues.Add({ path, "Cannot write asset metadata" });
					continue;
				}
			}
			if (asset.Id.IsValid())
			{
				const std::string id(asset.Id.ToString().View());
				if (!ids.emplace(id, std::string(path.View())).second)
				{
					m_Issues.Add({ path, "Duplicate asset ID" });
					duplicates.insert(id);
				}
			}
			asset.Fingerprint = FileHash(it->path());
			if (!asset.Fingerprint)
				m_Issues.Add({ path, "Cannot read asset" });
			asset.Fingerprint = Hash(std::string_view(asset.Settings.GenerateMipmaps ? "mips" : "no-mips"), asset.Fingerprint);
			asset.Fingerprint = Hash(std::to_string(static_cast<int>(asset.Settings.ColorSpace)), asset.Fingerprint);
			assets.emplace(std::string(path.View()), std::move(asset));
		}
		if (ec) m_Issues.Add({ m_Root, "Asset scan failed" });
		for (const auto& id : duplicates) ids.erase(id);
		for (const auto& [path, asset] : m_Assets)
		{
			if (asset.Id.IsValid() && ids.contains(std::string(asset.Id.ToString().View())))
				m_Aliases[path] = std::string(asset.Id.ToString().View());
		}
		m_Assets = std::move(assets);
		m_Ids = std::move(ids);
		for (auto& [path, asset] : m_Assets)
			ReadDependencies(asset);
		for (const auto& [path, asset] : m_Assets)
		{
			for (const auto& reference : asset.Dependencies)
			{
				if (!Find(reference))
					m_Issues.Add({ asset.Path, String("Missing dependency ") + reference });
			}
		}
		m_Fingerprints.clear();
		for (const auto& [path, asset] : m_Assets)
		{
			std::set<std::string> visited;
			m_Fingerprints[path] = FingerprintImpl(asset, visited);
		}
		return m_Issues.IsEmpty();
	}

	const AssetRecord* AssetDatabase::Find(std::string_view reference) const
	{
		if (IsReference(reference))
		{
			Guid id;
			if (!Guid::Parse(reference.substr(8), id) || !id.IsValid()) return nullptr;
			const auto found = m_Ids.find(std::string(id.ToString().View()));
			return found == m_Ids.end() ? nullptr : &m_Assets.at(found->second);
		}
		const auto found = m_Assets.find(std::string(Normalize(reference).View()));
		if (found != m_Assets.end()) return &found->second;
		const auto alias = m_Aliases.find(reference);
		if (alias != m_Aliases.end()) return Find("asset://" + alias->second);
		return nullptr;
	}

	String AssetDatabase::Resolve(std::string_view reference) const
	{
		if (const AssetRecord* asset = Find(reference)) return asset->Path;
		return IsReference(reference) ? String() : Normalize(reference);
	}

	String AssetDatabase::Reference(std::string_view path) const
	{
		if (const AssetRecord* asset = Find(path); asset && asset->Id.IsValid())
			return String("asset://") + asset->Id.ToString();
		return String(path);
	}

	uint64 AssetDatabase::FingerprintImpl(const AssetRecord& asset, std::set<std::string>& visited) const
	{
		std::vector<const AssetRecord*> pending{ &asset };
		while (!pending.empty())
		{
			const AssetRecord* current = pending.back();
			pending.pop_back();
			if (!visited.insert(std::string(current->Path.View())).second) continue;
			for (const String& dependency : current->Dependencies)
				if (const AssetRecord* found = Find(dependency)) pending.push_back(found);
		}
		uint64 hash = 14695981039346656037ull;
		for (const auto& path : visited)
		{
			const auto& current = m_Assets.at(path);
			hash = Hash(path, Hash(std::to_string(current.Fingerprint), hash));
			for (const String& dependency : current.Dependencies) hash = Hash(dependency, hash);
		}
		return hash;
	}

	uint64 AssetDatabase::Fingerprint(std::string_view reference) const
	{
		const AssetRecord* asset = Find(reference);
		const auto found = asset ? m_Fingerprints.find(std::string(asset->Path.View())) : m_Fingerprints.end();
		return found == m_Fingerprints.end() ? 0 : found->second;
	}

	Array<String> AssetDatabase::Dependents(std::string_view reference) const
	{
		Array<String> result;
		const String resolved = Resolve(reference);
		for (const auto& [path, asset] : m_Assets)
		{
			for (const auto& dependency : asset.Dependencies)
			{
				if (Resolve(dependency) == resolved && !resolved.IsEmpty())
					result.AddUnique(asset.Path);
			}
		}
		return result;
	}

	bool AssetDatabase::SetImportSettings(std::string_view reference, const AssetImportSettings& settings)
	{
		const AssetRecord* found = Find(reference);
		if (!m_Writable || !found || !IsTexture(found->Path)) return false;
		AssetRecord asset = *found;
		asset.Settings = settings;
		if (!SaveMetadata(asset)) return false;
		Refresh();
		return true;
	}

	bool AssetDatabase::Move(std::string_view source, std::string_view destination, String& error)
	{
		const String from = Normalize(source), to = Normalize(destination);
		std::error_code ec;
		if (!m_Writable || from.IsEmpty() || to.IsEmpty() || from == to || Inside(to, from) || fs::exists(Native(m_Root) / Native(to), ec) || fs::exists(Native(m_Root) / Native(to + MetadataExtension), ec))
		{
			error = "Invalid move or destination already exists";
			return false;
		}
		const fs::path oldPath = Native(m_Root) / Native(from), newPath = Native(m_Root) / Native(to);
		const bool directory = fs::is_directory(oldPath, ec);
		const auto root = fs::weakly_canonical(Native(m_Root), ec);
		const auto parent = fs::weakly_canonical(newPath.parent_path(), ec);
		const auto relativeParent = parent.lexically_relative(root);
		if (ec || relativeParent.empty() || *relativeParent.begin() == ".." || fs::is_symlink(oldPath, ec) || (!directory && !Find(from)))
		{
			error = "Move must stay inside Content and preserve valid metadata";
			return false;
		}
		for (const auto& [path, asset] : m_Assets)
		{
			if (NativeReferences(path)) continue;
			for (const String& dependency : asset.Dependencies)
			{
				const bool movedOwner = directory && Inside(path, from);
				if (movedOwner != Inside(Resolve(dependency), from))
				{
					error = String("Move would break relative references in ") + asset.Path;
					return false;
				}
			}
		}
		struct Rewrite { fs::path Path; String Before; String After; };
		std::vector<Rewrite> rewrites;
		for (const auto& [path, asset] : m_Assets)
		{
			if (!NativeReferences(path)) continue;
			Rewrite rewrite{ Native(m_Root) / Native(path), {}, {} };
			if (!ReadText(rewrite.Path, rewrite.Before)) { error = "Cannot read asset references"; return false; }
			for (const String& line : rewrite.Before.Split('\n', true))
			{
				auto tokens = FieldSerializer::Tokenize(line);
				bool changed = false;
				for (int32 i = 1; i < tokens.Num(); ++i)
				{
					if (!IsReference(tokens[i]) && asset.Dependencies.Contains(tokens[i]) && Inside(Resolve(tokens[i]), from))
					{
						tokens[i] = Reference(tokens[i]);
						changed = true;
					}
				}
				if (changed)
				{
					const auto indent = line.View().find_first_not_of(" \t");
					rewrite.After += line.Left(static_cast<int32>(indent));
					rewrite.After += tokens[0];
					for (int32 i = 1; i < tokens.Num(); ++i)
					{
						rewrite.After += ' ';
						FieldSerializer::AppendQuoted(rewrite.After, tokens[i]);
					}
				}
				else rewrite.After += line;
				rewrite.After += '\n';
			}
			if (rewrite.After.EndsWith("\n")) rewrite.After = rewrite.After.Left(rewrite.After.Length() - 1);
			if (rewrite.After != rewrite.Before) rewrites.push_back(std::move(rewrite));
		}
		auto restore = [&]() {
			for (const auto& rewrite : rewrites)
				if (!AtomicWrite(rewrite.Path, rewrite.Before)) error += " Reference rollback failed";
		};
		for (const auto& rewrite : rewrites)
		{
			if (!AtomicWrite(rewrite.Path, rewrite.After))
			{
				error = "Cannot update legacy references";
				restore();
				return false;
			}
		}
		fs::rename(oldPath, newPath, ec);
		if (ec) { error = "Could not move asset"; restore(); return false; }
		if (!directory)
		{
			fs::rename(Native(m_Root) / Native(from + MetadataExtension), Native(m_Root) / Native(to + MetadataExtension), ec);
			if (ec)
			{
				fs::rename(newPath, oldPath, ec);
				error = "Could not move asset metadata";
				restore();
				return false;
			}
		}
		Refresh();
		return true;
	}

	void AssetDatabase::ReadDependencies(AssetRecord& asset)
	{
		const String extension = Text(Native(asset.Path).extension()).ToLower();
		const bool native = extension == ".scene" || extension == ".material";
		if (!native && extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".mtl" && extension != ".html" && extension != ".htm" && extension != ".css" && extension != ".js") return;
		String text;
		if (!ReadText(Native(m_Root) / Native(asset.Path), text))
		{
			m_Issues.Add({ asset.Path, "Cannot read dependency document" });
			return;
		}
		auto add = [&](String reference, bool relative) {
			if (reference.IsEmpty() || reference.StartsWith("data:") || reference.StartsWith("#") || reference.StartsWith("http:") || reference.StartsWith("https:") || reference.StartsWith("//")) return;
			if (!IsReference(reference))
			{
				if (relative)
					reference = Text(Native(asset.Path).parent_path() / Native(reference));
				reference = Normalize(reference);
				if (reference.IsEmpty()) { m_Issues.Add({ asset.Path, "Dependency escapes Content" }); return; }
			}
			asset.Dependencies.AddUnique(reference);
		};
		if (native)
		{
			const ComponentInfo* component = nullptr;
			const ScriptType* script = nullptr;
			for (const auto& line : text.Split('\n'))
			{
				const auto tokens = FieldSerializer::Tokenize(line);
				if (tokens.Num() < 2) continue;
				if (tokens[0] == "component") { component = ComponentRegistry::Get().Find(tokens[1]); script = nullptr; continue; }
				if (component && std::string_view(component->Name) == "Scripts" && tokens[0] == "script")
				{
					script = ScriptRuntime::Get().FindType(tokens[1]);
					continue;
				}
				const FieldInfo* field = component ? component->FindField(tokens[0]) : nullptr;
				if (script)
					for (const auto& property : script->Fields)
						if (tokens[0] == property.Name) field = &property;
				const bool materialMap = extension == ".material" && tokens[0].EndsWith("Map");
				const bool materialSlot = component && std::string_view(component->Name) == "Material" && (tokens[0] == "Material" || tokens[0] == "Slot");
				if (materialMap || materialSlot || (field && field->AssetFilter))
				{
					const String& value = tokens.Last();
					if (tokens[0] == "Mesh" && value.Find(".") == INDEX_NONE && !IsReference(value)) continue;
					add(value, false);
				}
				else
				{
					for (const String& value : tokens)
						if (IsReference(value)) add(value, false);
				}
			}
		}
		else if (extension == ".gltf" || extension == ".glb")
		{
			if (extension == ".glb")
			{
				auto word = [&](int offset) {
					const auto* p = reinterpret_cast<const uint8*>(text.CStr()) + offset;
					return uint32(p[0]) | (uint32(p[1]) << 8) | (uint32(p[2]) << 16) | (uint32(p[3]) << 24);
				};
				if (text.Length() < 20 || word(0) != 0x46546c67 || word(4) != 2 || word(16) != 0x4e4f534a || word(12) > uint32(text.Length() - 20))
				{ m_Issues.Add({ asset.Path, "Invalid GLB header" }); return; }
				text = text.Substring(20, static_cast<int32>(word(12)));
			}
			const Json json = Json::Parse(text);
			if (!json.IsObject()) { m_Issues.Add({ asset.Path, "Invalid glTF JSON" }); return; }
			for (const char* collection : { "buffers", "images" })
				for (int32 i = 0; i < json[collection].Num(); ++i)
					add(json[collection][i]["uri"].AsString(), true);
		}
		else if (extension == ".obj" || extension == ".mtl")
		{
			for (const auto& line : text.Split('\n'))
			{
				const auto tokens = FieldSerializer::Tokenize(line);
				if (tokens.Num() > 1 && (tokens[0] == "mtllib" || tokens[0].StartsWith("map_") || tokens[0] == "bump"))
					add(tokens.Last(), true);
			}
		}
		else
		{
			static const std::regex links(R"re((?:src|href)\s*=\s*["']([^"'#?]+)(?:[?#][^"']*)?["']|url\(\s*["']?([^\s)'"?#]+)(?:[?#][^)'"\s]*)?["']?\s*\)|(?:from|import)\s*["'](\.[^"']+)["'])re", std::regex::icase);
			const std::string bytes(text.View());
			for (std::sregex_iterator it(bytes.begin(), bytes.end(), links), end; it != end; ++it)
				add(String((*it)[1].matched ? (*it)[1].str() : (*it)[2].matched ? (*it)[2].str() : (*it)[3].str()), true);
		}
		asset.Dependencies.Sort([](const String& a, const String& b) { return a.View() < b.View(); });
	}
}
