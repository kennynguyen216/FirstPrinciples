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
		Vec3,
		Color,
		String,
	};

	struct FieldInfo
	{
		const char* Name = nullptr;
		FieldType Type = FieldType::Float;
		usize Offset = 0;

		// Editor hints. AssetFilter makes a String a file picker (for example "*.png *.jpg").
		// Choices makes a String a drop down. Editable choices also allow typing a path.
		const char* AssetFilter = nullptr;
		const Array<const char*>* Choices = nullptr;
		bool ChoicesEditable = false;

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
		void (*Copy)(void* dst, const void* src) = nullptr;

		// Creates the EnTT storage from engine code. Storage made inside a loaded module would
		// keep that module's vtable and crash once the module unloads.
		void (*EnsureStorage)(entt::registry&) = nullptr;

		// Optional text format for components that are not a flat list of fields.
		// WriteCustom appends indented lines. ReadCustom gets every line that follows the component.
		void (*WriteCustom)(String& out, const void* component) = nullptr;
		void (*ReadCustom)(void* component, const Array<String>& tokens, std::string_view line) = nullptr;

		// The editor draws this component itself instead of listing fields.
		bool CustomInspector = false;

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
			else if constexpr (std::is_same_v<M, Vec3>) return FieldType::Vec3;
			else if constexpr (std::is_same_v<M, Color>) return FieldType::Color;
			else if constexpr (std::is_same_v<M, String>) return FieldType::String;
			else static_assert(sizeof(M) == 0, "Unsupported field type. Use bool, int32, float, Vec2, Vec3, Color or String.");
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

		template <typename T>
		void CopyComponent(void* dst, const void* src)
		{
			*static_cast<T*>(dst) = *static_cast<const T*>(src);
		}

		template <typename T>
		void EnsureComponentStorage(entt::registry& registry)
		{
			(void)registry.storage<T>();
		}
	}

	// Describes one member of any plain struct. Used for components and assets like materials.
	template <typename T, typename M>
	FieldInfo MakeField(const char* name, M T::*member)
	{
		// Offset of the member measured on a scratch instance.
		alignas(T) uint8 storage[sizeof(T)];
		T* object = reinterpret_cast<T*>(storage);
		FieldInfo field;
		field.Name = name;
		field.Type = Detail::FieldTypeOf<M>();
		field.Offset = static_cast<usize>(reinterpret_cast<uint8*>(&(object->*member)) - storage);
		return field;
	}

	// Builds a field list with chained calls. Hints apply to the last added field.
	template <typename T>
	class FieldListBuilder
	{
	public:
		explicit FieldListBuilder(Array<FieldInfo>& fields) : m_Fields(fields) {}

		template <typename M>
		FieldListBuilder& Field(const char* name, M T::*member)
		{
			m_Fields.Add(MakeField(name, member));
			return *this;
		}

		FieldListBuilder& AssetPath(const char* filter)
		{
			m_Fields.Last().AssetFilter = filter;
			return *this;
		}

		// The list must outlive the field, so pass a static.
		FieldListBuilder& Choices(const Array<const char*>& choices, bool editable = false)
		{
			m_Fields.Last().Choices = &choices;
			m_Fields.Last().ChoicesEditable = editable;
			return *this;
		}

	private:
		Array<FieldInfo>& m_Fields;
	};

	template <typename T>
	class ComponentBuilder
	{
	public:
		explicit ComponentBuilder(ComponentInfo& info) : m_Info(info), m_Fields(info.Fields) {}

		template <typename M>
		ComponentBuilder& Field(const char* name, M T::*member)
		{
			m_Fields.Field(name, member);
			return *this;
		}

		ComponentBuilder& AssetPath(const char* filter)
		{
			m_Fields.AssetPath(filter);
			return *this;
		}

		ComponentBuilder& Choices(const Array<const char*>& choices, bool editable = false)
		{
			m_Fields.Choices(choices, editable);
			return *this;
		}

		ComponentBuilder& HideInEditor()
		{
			m_Info.ShowInEditor = false;
			return *this;
		}

		ComponentBuilder& CustomSerializer(void (*write)(String&, const void*), void (*read)(void*, const Array<String>&, std::string_view))
		{
			m_Info.WriteCustom = write;
			m_Info.ReadCustom = read;
			return *this;
		}

		ComponentBuilder& CustomInspector()
		{
			m_Info.CustomInspector = true;
			return *this;
		}

	private:
		ComponentInfo& m_Info;
		FieldListBuilder<T> m_Fields;
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
			info->Copy = &Detail::CopyComponent<T>;
			info->EnsureStorage = &Detail::EnsureComponentStorage<T>;
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
