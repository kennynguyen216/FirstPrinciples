#include "Engine/Scripting/ScriptComponent.h"
#include "Engine/FieldSerializer.h"

namespace FP::ScriptComponentFormat
{
	void Write(String& out, const void* component)
	{
		for (const ScriptSlot& slot : static_cast<const ScriptComponent*>(component)->Scripts)
		{
			out += "  script ";
			FieldSerializer::AppendQuoted(out, slot.Name);
			out += '\n';
			for (const String& property : slot.Properties.Split('\n'))
				out.Appendf("    %s\n", property.Trimmed().CStr());
		}
	}

	void Read(void* component, const Array<String>& tokens, std::string_view line)
	{
		Array<ScriptSlot>& scripts = static_cast<ScriptComponent*>(component)->Scripts;
		if (tokens[0] == "script")
		{
			scripts.Emplace(tokens.Num() > 1 ? tokens[1] : String());
			return;
		}
		if (scripts.IsEmpty())
			return;
		// Keep the line as written so values round trip exactly, even for unknown scripts.
		ScriptSlot& slot = scripts.Last();
		slot.Properties.Append(String(line).Trimmed().View());
		slot.Properties += '\n';
	}
}
