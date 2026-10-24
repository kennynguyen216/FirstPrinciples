#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/Audio/VoiceChat.h"

#include <SDL3/SDL.h>

using namespace FP;
namespace FP { bool VoiceChatOfflineSelfCheck(); bool VoiceChatLoopbackSelfCheck(); }

FP_TEST(VoiceChat_DefaultDoesNotOpenMicrophone)
{
	const auto initialized = SDL_WasInit(SDL_INIT_AUDIO);
	{
		VoiceChat voice;
		FP_EXPECT(!voice.IsEnabled());
		FP_EXPECT(voice.GetStatusText().Contains("Microphone off"));
		voice.Shutdown();
	}
	FP_EXPECT(SDL_WasInit(SDL_INIT_AUDIO) == initialized);
}

FP_TEST(VoiceChat_ProtocolOwnershipBoundsMuteAndCleanup)
{
	Engine::RegisterEngineComponents();
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
	const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	FP_EXPECT(VoiceChatOfflineSelfCheck());
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}

FP_TEST(VoiceChat_EncryptedLoopbackCaptureProximityMuteAndPlayback)
{
	Engine::RegisterEngineComponents();
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
	const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	FP_EXPECT(VoiceChatLoopbackSelfCheck());
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
