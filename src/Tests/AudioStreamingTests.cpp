#include "Tests/TestFramework.h"
#include "Engine/Audio/AudioCapture.h"
#include "Engine/Audio/AudioDevice.h"

#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <limits>

using namespace FP;
namespace FP { bool AudioStreamingOfflineSelfCheck(); bool AudioCaptureOfflineSelfCheck(); }

FP_TEST(AudioStreaming_RingUnderflowPanFilterAndBus)
{
	FP_EXPECT(AudioStreamingOfflineSelfCheck());
	FP_EXPECT(AudioCaptureOfflineSelfCheck());
}

FP_TEST(AudioStreaming_BoundedBatchesHandlesAndExplicitCapture)
{
	const char* hint = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
	const String saved = hint ? hint : "";
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	{
		AudioDevice device;
		FP_EXPECT(device.Initialize());
		AudioPlaybackParams params; params.Bus = AudioBus::Voice;
		FP_EXPECT(!device.CreateStream(1, 7999, params));
		const auto voice = device.CreateStream(1, 48000, params);
		FP_EXPECT(voice);
		if (voice)
		{
			FP_EXPECT(device.Pause(voice.Voice));
			std::array<float, 12001> samples{};
			FP_EXPECT(!device.QueueStream(voice.Voice, samples.data(), uint32(samples.size())));
			samples[500] = std::numeric_limits<float>::quiet_NaN();
			FP_EXPECT(!device.QueueStream(voice.Voice, samples.data(), 960));
			samples[500] = .5f;
			FP_EXPECT(device.QueueStream(voice.Voice, samples.data(), 12000));
			FP_EXPECT(!device.QueueStream(voice.Voice, samples.data(), 1));
			FP_EXPECT(device.Resume(voice.Voice));
			params.LowPassHz = 800;
			FP_EXPECT(device.SetParameters(voice.Voice, params));
			params.LowPassHz = std::numeric_limits<float>::infinity();
			FP_EXPECT(!device.SetParameters(voice.Voice, params));
			device.StopContext(1);
			FP_EXPECT(!device.QueueStream(voice.Voice, samples.data(), 1));
			FP_EXPECT(device.GetState(voice.Voice) == VoiceState::Invalid);
		}
		AudioCapture capture;
		FP_EXPECT(!capture.IsReady());
		std::array<float, 960> recorded{};
		FP_EXPECT(capture.Read(recorded.data(), uint32(recorded.size())) == 0);
		FP_EXPECT(capture.Initialize());
		uint32 received = 0;
		for (int i = 0; i < 12 && !received; ++i) { SDL_Delay(10); received = capture.Read(recorded.data(), uint32(recorded.size())); }
		FP_EXPECT(received > 0 && received <= recorded.size());
		for (uint32 i = 0; i < received; ++i) FP_EXPECT(std::isfinite(recorded[i]) && std::abs(recorded[i]) <= 1);
		capture.Shutdown();
		FP_EXPECT(!capture.IsReady() && device.IsReady());
		FP_EXPECT(capture.Read(recorded.data(), uint32(recorded.size())) == 0);
	}
	if (saved.IsEmpty()) SDL_ResetHint(SDL_HINT_AUDIO_DRIVER); else SDL_SetHint(SDL_HINT_AUDIO_DRIVER, saved.CStr());
}
