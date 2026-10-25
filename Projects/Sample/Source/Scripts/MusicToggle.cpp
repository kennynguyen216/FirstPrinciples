#include "Engine/Scripting/Script.h"

using namespace FP;

// M starts or stops the music source in the audio playground.
class MusicToggle : public Script
{
public:
	void OnUpdate(float) override
	{
		if (!Input::WasKeyPressed(SDL_SCANCODE_M)) return;
		auto* source = TryGet<AudioSourceComponent>();
		if (!source || source->Clip.IsEmpty()) return;
		source->Enabled = !source->Enabled;
		if (source->Enabled)
		{
			const auto played = GetWorld().GetAudio().Play(GetEntity());
			if (!played) { source->Enabled = false; FP_LOG_WARN("Music: %s", played.Result.Message.CStr()); }
		}
		else GetWorld().GetAudio().StopSource(GetEntity());
	}
};

FP_SCRIPT(MusicToggle);
