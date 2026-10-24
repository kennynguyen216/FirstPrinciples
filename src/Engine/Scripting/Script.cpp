#include "Engine/Scripting/Script.h"

namespace FP::ScriptTypes
{
	Array<ScriptType*>& GetModuleList()
	{
		static Array<ScriptType*> types;
		return types;
	}
}
