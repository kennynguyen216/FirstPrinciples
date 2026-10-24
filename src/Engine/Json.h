#pragma once

#include "Core/CoreMinimal.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FP
{
	// A parsed JSON value, for reading data pages send with engine.send. Reading a missing key
	// or the wrong type gives an empty value or a default, never an error:
	//
	//   const Json data = Json::Parse(text);
	//   const float volume = data["volume"].AsNumber(1);
	//   const String name = data["player"]["name"].AsString();
	class Json
	{
	public:
		enum class Type : uint8
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object,
		};

		Json() = default;

		// Null when text is not valid JSON.
		static Json Parse(std::string_view text);

		Type GetType() const { return m_Type; }
		bool IsNull() const { return m_Type == Type::Null; }
		bool IsNumber() const { return m_Type == Type::Number; }
		bool IsString() const { return m_Type == Type::String; }
		bool IsBool() const { return m_Type == Type::Bool; }
		bool IsArray() const { return m_Type == Type::Array; }
		bool IsObject() const { return m_Type == Type::Object; }

		double AsNumber(double fallback = 0) const { return m_Type == Type::Number ? m_Number : fallback; }
		bool AsBool(bool fallback = false) const { return m_Type == Type::Bool ? m_Bool : fallback; }
		String AsString(std::string_view fallback = {}) const { return String(m_Type == Type::String ? std::string_view(m_String) : fallback); }

		// Object members. A missing key gives a null value.
		const Json& operator[](std::string_view key) const;
		bool Has(std::string_view key) const;

		// Array items. Out of range gives a null value.
		const Json& operator[](int32 index) const;
		int32 Num() const { return m_Type == Type::Array ? static_cast<int32>(m_Items.size()) : 0; }

	private:
		friend class JsonParser;

		Type m_Type = Type::Null;
		bool m_Bool = false;
		double m_Number = 0;
		std::string m_String;
		std::vector<Json> m_Items;
		std::map<std::string, Json, std::less<>> m_Members;
	};
}
