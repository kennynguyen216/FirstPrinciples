#include "Tests/TestFramework.h"
#include "Engine/Engine.h"

using namespace FP;

namespace
{
	const VoiceHandle HostVoice{17, 23, 0};

	class HostDispatchAudio : public AudioWorld
	{
	public:
		explicit HostDispatchAudio(World& world) : AudioWorld(world) {}
		~HostDispatchAudio() override { ++Destroyed; }
		static inline int Destroyed = 0;
		mutable uint32 Calls = 0;
		bool Attach(AudioDevice*) override { Calls |= 1u << 0; return true; }
		AudioDevice* GetDevice() const override { Calls |= 1u << 1; return nullptr; }
		void BeginPlay() override { Calls |= 1u << 2; }
		void EndPlay() override { Calls |= 1u << 3; }
		void Update() override { Calls |= 1u << 4; }
		AudioResult Preload(const String&) override { Calls |= 1u << 5; return {}; }
		AudioPlayResult Play(Entity) override { Calls |= 1u << 6; return {HostVoice, {}}; }
		AudioPlayResult PlayOneShot(const String&, const OneShotParams&) override { Calls |= 1u << 7; return {HostVoice, {}}; }
		bool Stop(VoiceHandle) override { Calls |= 1u << 8; return true; }
		bool Pause(VoiceHandle) override { Calls |= 1u << 9; return true; }
		bool Resume(VoiceHandle) override { Calls |= 1u << 10; return true; }
		VoiceState GetState(VoiceHandle) const override { Calls |= 1u << 11; return VoiceState::Playing; }
		VoiceHandle GetSourceVoice(Entity) const override { Calls |= 1u << 12; return HostVoice; }
		void StopSource(Entity) override { Calls |= 1u << 13; }
		void StopAll() override { Calls |= 1u << 14; }
		Entity GetListenerEntity() const override { Calls |= 1u << 15; return entt::entity{7}; }
	};

	class HostDispatchWorld : public World
	{
		FP_OBJECT(HostDispatchWorld, World)
	public:
		HostDispatchWorld() : Service(std::make_unique<HostDispatchAudio>(*this)) {}
		AudioWorld& GetAudio() override { ++GetterCalls; return *Service; }
		std::unique_ptr<AudioWorld> Service;
		int GetterCalls = 0;
	};

	// A module knows these base interfaces; every operation must reach host-created vtables.
	FP_NOINLINE void CallAudioThroughScriptInterfaces(World& world)
	{
		AudioWorld& audio = world.GetAudio();
		FP_EXPECT(audio.Attach(nullptr));
		FP_EXPECT(audio.GetDevice() == nullptr);
		audio.BeginPlay();
		audio.EndPlay();
		audio.Update();
		FP_EXPECT(audio.Preload("Audio/Uncached.wav"));
		FP_EXPECT(audio.Play(NullEntity).Voice == HostVoice);
		FP_EXPECT(audio.PlayOneShot("Audio/Uncached.wav").Voice == HostVoice);
		FP_EXPECT(audio.Stop(HostVoice));
		FP_EXPECT(audio.Pause(HostVoice));
		FP_EXPECT(audio.Resume(HostVoice));
		FP_EXPECT(audio.GetState(HostVoice) == VoiceState::Playing);
		FP_EXPECT(audio.GetSourceVoice(NullEntity) == HostVoice);
		audio.StopSource(NullEntity);
		audio.StopAll();
		FP_EXPECT(audio.GetListenerEntity() == entt::entity{7});
	}
}

FP_TEST(Audio_ScriptInterfacesDispatchToHost)
{
	Engine::RegisterEngineComponents();
	HostDispatchAudio::Destroyed = 0;
	StrongObjectPtr<HostDispatchWorld> world(NewObject<HostDispatchWorld>());
	CallAudioThroughScriptInterfaces(*world.Get());
	FP_EXPECT(world->GetterCalls == 1);
	FP_EXPECT(static_cast<HostDispatchAudio*>(world->Service.get())->Calls == 0xffffu);
	world.Reset();
	GarbageCollector::Get().Collect();
	FP_EXPECT(HostDispatchAudio::Destroyed == 1);
}
