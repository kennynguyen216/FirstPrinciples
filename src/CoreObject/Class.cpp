#include "CoreObject/Class.h"

namespace FP
{
	namespace
	{
		Array<const Class*>& GetRegistry()
		{
			static Array<const Class*> classes;
			return classes;
		}
	}

	Class::Class(const char* name, const Class* super, usize size, usize alignment, ConstructFn construct)
		: m_Name(name), m_Super(super), m_Size(size), m_Alignment(alignment), m_Construct(construct)
	{
		FP_CHECK(Find(name) == nullptr, "Two classes are named %s", name);
		GetRegistry().Add(this);
	}

	bool Class::IsChildOf(const Class* other) const
	{
		for (const Class* cls = this; cls; cls = cls->m_Super)
		{
			if (cls == other)
				return true;
		}
		return false;
	}

	const Class* Class::Find(std::string_view name)
	{
		for (const Class* cls : GetRegistry())
		{
			if (name == cls->m_Name)
				return cls;
		}
		return nullptr;
	}

	const Array<const Class*>& Class::GetAll()
	{
		return GetRegistry();
	}
}
