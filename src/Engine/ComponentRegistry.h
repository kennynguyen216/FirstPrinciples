#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

#include <entt/entt.hpp>

#include <type_traits>

namespace FP
{
	// Field types the editor and the scene format understand.
	enum class FieldType : uint8
	{
		Bool,
		Int,
		Float,
		Vec2,
		Color,
		String,
	};

	struct FieldInfo
	{
		const char* Name;
		FieldType Type;
		usize Offset;

		template <typename T>
		T& Get(void* component) const { return *reinterpret_cast<T*>(static_cast<uint8*>(component) + Offset); }
	};

	// Type erased access to one component type. Lets the editor and serializer work with any
	// registered component without knowing its C++ type.
	struct ComponentInfo
	{
		const char* Name = nullptr;
		entt::id_type TypeId = 0;
		Array<FieldInfo> Fields;
		bool ShowInEditor = true;

		void* (*TryGet)(entt::registry&, entt::entity) = nullptr;
		void* (*Add)(entt::registry&, entt::entity) = nullptr;
		void (*Remove)(entt::registry&, entt::entity) = nullptr;

		const FieldInfo* FindField(std::string_view name) const
		{
			for (const FieldInfo& field : Fields)
			{
				if (name == field.Name)
					return &field;
			}
			return nullptr;
		}
	};

	namespace Detail
	{
		template <typename M>
		constexpr FieldType FieldTypeOf()
		{
			if constexpr (std::is_same_v<M, bool>) return FieldType::Bool;
			else if constexpr (std::is_same_v<M, int32>) return FieldType::Int;
			else if constexpr (std::is_same_v<M, float>) return FieldType::Float;
			else if constexpr (std::is_same_v<M, Vec2>) return FieldType::Vec2;
			else if constexpr (std::is_same_v<M, Color>) return FieldType::Color;
			else if constexpr (std::is_same_v<M, String>) return FieldType::String;
			else static_assert(sizeof(M) == 0, "Unsupported field type. Use bool, int32, float, Vec2, Color or String.");
		}

		// EnTT stores no instance for empty types so hand out a shared dummy instead.
		template <typename T>
		void* TryGetComponent(entt::registry& registry, entt::entity entity)
		{
			if constexpr (std::is_empty_v<T>)
			{
				static T dummy;
				return registry.all_of<T>(entity) ? &dummy : nullptr;
			}
			else
			{
				return registry.try_get<T>(entity);
			}
		}

		template <typename T>
		void* AddComponent(entt::registry& registry, entt::entity entity)
		{
			if (!registry.all_of<T>(entity))
				registry.emplace<T>(entity);
			return TryGetComponent<T>(registry, entity);
		}

		template <typename T>
		void RemoveComponent(entt::registry& registry, entt::entity entity)
		{
			registry.remove<T>(entity);
		}
	}

	template <typename T>
	class ComponentBuilder
	{
	public:
		explicit ComponentBuilder(ComponentInfo& info) : m_Info(info) {}

		template <typename M>
		ComponentBuilder& Field(const char* name, M T::*member)
		{
			// Offset of the member measured on a scratch instance.
			alignas(T) uint8 storage[sizeof(T)];
			T* object = reinterpret_cast<T*>(storage);
			const usize offset = static_cast<usize>(reinterpret_cast<uint8*>(&(object->*member)) - storage);
			m_Info.Fields.Add({ name, Detail::FieldTypeOf<M>(), offset });
			return *this;
		}

		ComponentBuilder& HideInEditor()
		{
			m_Info.ShowInEditor = false;
			return *this;
		}

	private:
		ComponentInfo& m_Info;
	};

	// Every component the editor or scene files should see is registered here once at startup.
	class ComponentRegistry
	{
	public:
		static ComponentRegistry& Get();

		template <typename T>
		ComponentBuilder<T> Register(const char* name)
		{
			const entt::id_type id = entt::type_hash<T>::value();
			FP_CHECK(!Find(name) && !FindById(id), "Component %s registered twice", name);

			ComponentInfo* info = new ComponentInfo();
			info->Name = name;
			info->TypeId = id;
			info->TryGet = &Detail::TryGetComponent<T>;
			info->Add = &Detail::AddComponent<T>;
			info->Remove = &Detail::RemoveComponent<T>;
			m_Components.Add(info);
			return ComponentBuilder<T>(*info);
		}

		const ComponentInfo* Find(std::string_view name) const;
		const ComponentInfo* FindById(entt::id_type id) const;

		template <typename T>
		const ComponentInfo* Find() const { return FindById(entt::type_hash<T>::value()); }

		const Array<ComponentInfo*>& GetAll() const { return m_Components; }

	private:
		Array<ComponentInfo*> m_Components;
	};
}
