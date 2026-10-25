#pragma once

#include "Core/String/String.h"
#include "Engine/Math.h"

#include <map>
#include <string>
#include <variant>

namespace FP
{
	// Named values for progress and settings. Save a separate slot for each profile or purpose.
	class SaveData
	{
	public:
		void SetBool(std::string_view key, bool value) { Set(key, value); }
		void SetInt(std::string_view key, int32 value) { Set(key, value); }
		void SetFloat(std::string_view key, float value) { Set(key, value); }
		void SetVec2(std::string_view key, Vec2 value) { Set(key, value); }
		void SetVec3(std::string_view key, Vec3 value) { Set(key, value); }
		void SetColor(std::string_view key, Color value) { Set(key, value); }
		void SetString(std::string_view key, std::string_view value) { Set(key, String(value)); }

		// Missing keys and values of another type give the fallback.
		bool GetBool(std::string_view key, bool fallback = false) const { return Get(key, fallback); }
		int32 GetInt(std::string_view key, int32 fallback = 0) const { return Get(key, fallback); }
		float GetFloat(std::string_view key, float fallback = 0) const { return Get(key, fallback); }
		Vec2 GetVec2(std::string_view key, Vec2 fallback = {}) const { return Get(key, fallback); }
		Vec3 GetVec3(std::string_view key, Vec3 fallback = {}) const { return Get(key, fallback); }
		Color GetColor(std::string_view key, Color fallback = {}) const { return Get(key, fallback); }
		String GetString(std::string_view key, std::string_view fallback = {}) const { return Get(key, String(fallback)); }

		bool Has(std::string_view key) const { return m_Values.find(key) != m_Values.end(); }
		bool Remove(std::string_view key);
		void Clear() { m_Values.clear(); }
		int32 Num() const { return static_cast<int32>(m_Values.size()); }

		// Slots use 1 to 64 lowercase letters, digits, underscores or hyphens.
		// Files live in Paths::GetSaveDir(), outside the project's content and install folder.
		bool Save(std::string_view slot) const;
		bool Load(std::string_view slot);
		static bool Exists(std::string_view slot);
		static bool Delete(std::string_view slot);

		// Empty when a value is not finite or the data exceeds 16 MiB.
		String SaveToString() const;
		// A failed load leaves the current values alone.
		bool LoadFromString(std::string_view text);

		// Explicit paths for tools. Saving creates parent folders and replaces the file only
		// after the new data has been written and flushed. Failures leave the old file alone.
		bool SaveToFile(const String& path) const;
		bool LoadFromFile(const String& path);

	private:
		using Value = std::variant<bool, int32, float, Vec2, Vec3, Color, String>;

		template <typename T>
		void Set(std::string_view key, T value) { m_Values[std::string(key)] = std::move(value); }

		template <typename T>
		T Get(std::string_view key, T fallback) const
		{
			const auto found = m_Values.find(key);
			if (found != m_Values.end())
			{
				if (const T* value = std::get_if<T>(&found->second))
					return *value;
			}
			return fallback;
		}

		std::map<std::string, Value, std::less<>> m_Values;
	};
}
