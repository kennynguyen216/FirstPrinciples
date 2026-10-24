#pragma once

#include "Core/CoreMinimal.h"

#include <type_traits>

namespace FP
{
	class Object;

	// Runtime type info for an Object subclass. One static instance per class, made by FP_OBJECT.
	class Class
	{
	public:
		using ConstructFn = Object* (*)(void* memory);

		Class(const char* name, const Class* super, usize size, usize alignment, ConstructFn construct);

		const char* GetName() const { return m_Name; }
		const Class* GetSuper() const { return m_Super; }
		usize GetSize() const { return m_Size; }
		usize GetAlignment() const { return m_Alignment; }
		bool IsAbstract() const { return m_Construct == nullptr; }

		bool IsChildOf(const Class* other) const;

		// Placement constructs a default instance in memory. Use NewObject instead of calling this.
		Object* Construct(void* memory) const { return m_Construct(memory); }

		// Every class registers itself by name at startup so saved data can find it again.
		static const Class* Find(std::string_view name);
		static const Array<const Class*>& GetAll();

	private:
		const char* m_Name;
		const Class* m_Super;
		usize m_Size;
		usize m_Alignment;
		ConstructFn m_Construct;
	};

	namespace Detail
	{
		template <typename T>
		Object* ConstructObject(void* memory)
		{
			return new (memory) T();
		}

		template <typename T>
		constexpr Class::ConstructFn GetConstructFn()
		{
			if constexpr (std::is_abstract_v<T> || !std::is_default_constructible_v<T>)
				return nullptr;
			else
				return &ConstructObject<T>;
		}
	}
}

// Put this at the top of every Object subclass body. It adds StaticClass, GetClass and Super.
#define FP_OBJECT(ThisClass, SuperClass)                                                          \
public:                                                                                           \
	using Super = SuperClass;                                                                     \
	static const ::FP::Class* StaticClass()                                                       \
	{                                                                                             \
		static const ::FP::Class s_Class(#ThisClass, SuperClass::StaticClass(), sizeof(ThisClass), \
			alignof(ThisClass), ::FP::Detail::GetConstructFn<ThisClass>());                       \
		return &s_Class;                                                                          \
	}                                                                                             \
	const ::FP::Class* GetClass() const override { return StaticClass(); }                       \
                                                                                                  \
private:                                                                                          \
	static inline const ::FP::Class* s_AutoRegister = StaticClass();                              \
                                                                                                  \
public:
