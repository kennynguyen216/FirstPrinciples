#include "Core/Misc/ModuleContext.h"

namespace FP
{
	namespace
	{
		const ModuleContext* GContext = nullptr;
	}

	const ModuleContext* GetModuleContext() { return GContext; }
	void SetModuleContext(const ModuleContext* context) { GContext = context; }
}
