#include "Engine/SaveData.h"
#include "Engine/FieldSerializer.h"
#include "Engine/Paths.h"
#include "Core/Misc/Guid.h"

#include <cstdio>
#include <filesystem>
#include <memory>

#if FP_PLATFORM_WINDOWS
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace FP
{
	namespace
	{
		namespace fs = std::filesystem;
		constexpr usize MaxSaveSize = 16 * 1024 * 1024;
		constexpr const char* TypeNames[] = { "Bool", "Int", "Float", "Vec2", "Vec3", "Color", "String" };

		fs::path Native(const String& path) { return fs::path(reinterpret_cast<const char8_t*>(path.CStr())); }

		String SlotPath(std::string_view slot)
		{
			if (slot.empty() || slot.size() > 64)
				return {};
			for (char c : slot)
			{
				if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-')
					return {};
			}
			const String dir = Paths::GetSaveDir();
			// A prefix also keeps Windows device names like con usable as slot names.
			return dir.IsEmpty() ? String() : dir + "/slot-" + slot + ".save";
		}

		std::FILE* Open(const fs::path& path, bool write)
		{
#if FP_PLATFORM_WINDOWS
			return _wfopen(path.c_str(), write ? L"wbx" : L"rb");
#else
			return std::fopen(path.c_str(), write ? "wbx" : "rb");
#endif
		}

		template <typename T>
		bool IsFinite(const T& value)
		{
			if constexpr (std::is_same_v<T, float>) return std::isfinite(value);
			else if constexpr (std::is_same_v<T, Vec2>) return std::isfinite(value.X) && std::isfinite(value.Y);
			else if constexpr (std::is_same_v<T, Vec3>) return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
			else if constexpr (std::is_same_v<T, Color>) return std::isfinite(value.R) && std::isfinite(value.G) && std::isfinite(value.B) && std::isfinite(value.A);
			else return true;
		}
	}

	bool SaveData::Remove(std::string_view key)
	{
		const auto found = m_Values.find(key);
		if (found == m_Values.end())
			return false;
		m_Values.erase(found);
		return true;
	}

	String SaveData::SaveToString() const
	{
		String out = "# FirstPrinciples save\nFPSave 1\n";
		for (const auto& [key, value] : m_Values)
		{
			const bool valid = std::visit([&](const auto& item) {
				if (!IsFinite(item))
					return false;
				const FieldType type = Detail::FieldTypeOf<std::decay_t<decltype(item)>>();
				out += TypeNames[static_cast<usize>(type)];
				out += ' ';
				FieldSerializer::AppendQuoted(out, String(key));
				FieldSerializer::Write(out, { "", type, 0 }, &item, "");
				return true;
			}, value);
			if (!valid || out.Length() > MaxSaveSize - 4)
				return {};
		}
		out += "end\n";
		return out;
	}

	bool SaveData::LoadFromString(std::string_view text)
	{
		if (text.size() > MaxSaveSize)
			return false;
		SaveData loaded;
		bool header = false;
		bool ended = false;
		const bool valid = FieldSerializer::ForEachLine(text, [&](int32, const Array<String>& tokens) {
			if (!header)
			{
				header = tokens.Num() == 2 && tokens[0] == "FPSave" && tokens[1] == "1";
				return header;
			}
			if (ended)
				return false;
			if (tokens.Num() == 1 && tokens[0] == "end")
			{
				ended = true;
				return true;
			}
			if (tokens.Num() < 3 || loaded.Has(tokens[1].View()))
				return false;
			Value value;
			if (tokens[0] == "Bool") value = bool{};
			else if (tokens[0] == "Int") value = int32{};
			else if (tokens[0] == "Float") value = float{};
			else if (tokens[0] == "Vec2") value = Vec2{};
			else if (tokens[0] == "Vec3") value = Vec3{};
			else if (tokens[0] == "Color") value = Color{};
			else if (tokens[0] == "String") value = String{};
			else return false;
			Array<String> fieldTokens = tokens;
			fieldTokens.RemoveAt(1);
			if (!std::visit([&](auto& item) {
				const FieldType type = Detail::FieldTypeOf<std::decay_t<decltype(item)>>();
				return FieldSerializer::Read({ "", type, 0 }, &item, fieldTokens);
			}, value))
				return false;
			loaded.m_Values.emplace(std::string(tokens[1].View()), std::move(value));
			return true;
		});
		if (!valid || !header || !ended)
			return false;
		m_Values = std::move(loaded.m_Values);
		return true;
	}

	bool SaveData::SaveToFile(const String& path) const
	{
		const String text = SaveToString();
		if (path.IsEmpty() || path.Contains(std::string_view("\0", 1)) || text.IsEmpty())
			return false;
		const fs::path target = Native(path);
		std::error_code ec;
		if (!target.parent_path().empty())
			fs::create_directories(target.parent_path(), ec);
		if (ec)
			return false;
		const fs::path staged = target.parent_path() / Native(String(".save-") + Guid::New().ToString() + ".tmp");
		struct Cleanup
		{
			fs::path Path;
			~Cleanup() { std::error_code ec; if (!Path.empty()) fs::remove(Path, ec); }
		} cleanup;
		using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
		File output(Open(staged, true), &std::fclose);
		if (!output)
			return false;
		cleanup.Path = staged;
		if (std::fwrite(text.CStr(), 1, text.Length(), output.get()) != static_cast<usize>(text.Length()) || std::fflush(output.get()) != 0)
			return false;
#if FP_PLATFORM_WINDOWS
		const int flushed = _commit(_fileno(output.get()));
#else
		const int flushed = fsync(fileno(output.get()));
#endif
		const int closed = std::fclose(output.release());
		if (flushed != 0 || closed != 0)
			return false;
		// Rename on the same disk keeps a failed write from damaging the previous save.
#if FP_PLATFORM_WINDOWS
		return MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
		fs::rename(staged, target, ec);
		return !ec;
#endif
	}

	bool SaveData::LoadFromFile(const String& path)
	{
		if (path.IsEmpty() || path.Contains(std::string_view("\0", 1)))
			return false;
		std::error_code ec;
		const fs::path source = Native(path);
		if (!fs::is_regular_file(source, ec) || fs::file_size(source, ec) > MaxSaveSize || ec)
			return false;
		using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
		File input(Open(source, false), &std::fclose);
		if (!input)
			return false;
		String text;
		char buffer[4096];
		while (const usize count = std::fread(buffer, 1, sizeof(buffer), input.get()))
		{
			if (text.Length() + count > MaxSaveSize)
				return false;
			text.Append(std::string_view(buffer, count));
		}
		return !std::ferror(input.get()) && LoadFromString(text.View());
	}

	bool SaveData::Save(std::string_view slot) const
	{
		const String path = SlotPath(slot);
		return !path.IsEmpty() && SaveToFile(path);
	}

	bool SaveData::Load(std::string_view slot)
	{
		const String path = SlotPath(slot);
		return !path.IsEmpty() && LoadFromFile(path);
	}

	bool SaveData::Exists(std::string_view slot)
	{
		const String path = SlotPath(slot);
		std::error_code ec;
		return !path.IsEmpty() && fs::is_regular_file(Native(path), ec);
	}

	bool SaveData::Delete(std::string_view slot)
	{
		const String path = SlotPath(slot);
		std::error_code ec;
		return !path.IsEmpty() && fs::is_regular_file(Native(path), ec) && fs::remove(Native(path), ec);
	}
}
