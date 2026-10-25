#include "Engine/Scripting/Script.h"
#include "Title/TitleComponents.h"

using namespace FP;

// Hands what the player picks on the title screen page to TitleSystem. Page events reach scripts
// rather than systems, so this only passes them on.
class TitleScreen : public Script
{
public:
	void OnUIEvent(std::string_view name, const Json& data) override
	{
		TitleRequestComponent* request = TryGet<TitleRequestComponent>();
		if (!request)
			request = &Add<TitleRequestComponent>();
		request->Action = String(name);
		request->Address = data["address"].AsString();
	}
};

FP_SCRIPT(TitleScreen);
