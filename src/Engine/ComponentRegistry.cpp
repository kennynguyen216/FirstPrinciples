#include "Engine/ComponentRegistry.h"
#include "Core/Misc/ModuleContext.h"

namespace FP
{
	ComponentRegistry& ComponentRegistry::Get()
	{
		static ComponentRegistry registry;
		return ResolveShared(&ModuleContext::ComponentRegistry, registry);
	}

	const ComponentInfo* ComponentRegistry::Find(std::string_view name) const
	{
		for (const ComponentInfo* info : m_Components)
		{
			if (name == info->Name)
				return info;
		}
		return nullptr;
	}

	const ComponentInfo* ComponentRegistry::FindById(entt::id_type id) const
	{
		for (const ComponentInfo* info : m_Components)
		{
			if (info->TypeId == id)
				return info;
		}
		return nullptr;
	}
}
