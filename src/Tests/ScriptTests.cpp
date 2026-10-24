#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"

using namespace FP;

namespace
{
	// Lives in the test executable, so it shows up next to the scripts from the loaded module.
	class TestCounter : public Script
	{
	public:
		static inline int Starts = 0;
		static inline int Reloads = 0;
		static inline int Stops = 0;
		static inline int Alive = 0;

		int32 Step = 1;
		int32 Count = 0;
		String Label = "counter";

		TestCounter() { ++Alive; }
		~TestCounter() override { --Alive; }

		void OnStart() override { ++Starts; }
		void OnHotReload() override { ++Reloads; }
		void OnUpdate(float) override { Count += Step; }
		void OnStop() override { ++Stops; }
	};

	void ResetCounters()
	{
		TestCounter::Starts = TestCounter::Reloads = TestCounter::Stops = 0;
	}
}

FP_SCRIPT(TestCounter)
{
	fields.Field("Step", &TestCounter::Step);
	fields.Field("Count", &TestCounter::Count);
	fields.Field("Label", &TestCounter::Label);
}

namespace
{
	TestCounter* FirstCounter(World& world, Entity entity)
	{
		return static_cast<TestCounter*>(world.Get<ScriptComponent>(entity).Scripts[0].Instance);
	}
}

FP_TEST(Script_ComponentRoundTripsUnknownScripts)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Scripted");
	ScriptComponent& scripts = world->Add<ScriptComponent>(e);
	scripts.Scripts.Emplace("TestCounter", "Step 3\nLabel \"has space\"\n");
	scripts.Scripts.Emplace("NotCompiledYet", "Speed 7\n");

	const String text = SceneSerializer::SaveToString(*world.Get());
	FP_EXPECT(text.Contains("script \"NotCompiledYet\""));

	StrongObjectPtr<World> loaded(NewObject<World>());
	FP_EXPECT(SceneSerializer::LoadFromString(*loaded.Get(), text.View()));
	const ScriptComponent& back = loaded->Get<ScriptComponent>(loaded->FindEntityByName("Scripted"));
	FP_EXPECT(back.Scripts.Num() == 2);
	FP_EXPECT(back.Scripts[1].Name == "NotCompiledYet");
	FP_EXPECT(back.Scripts[1].Properties == "Speed 7\n");
	FP_EXPECT(SceneSerializer::SaveToString(*loaded.Get()) == text);
}

FP_TEST(Script_SystemRunsLifecycle)
{
	Engine::RegisterEngineComponents();
	ResetCounters();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Scripted");
	world->Add<ScriptComponent>(e).Scripts.Emplace("TestCounter", "Step 2\nLabel \"from scene\"\n");

	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	FP_EXPECT(TestCounter::Starts == 1);
	TestCounter* counter = FirstCounter(*world.Get(), e);
	FP_EXPECT(counter && counter->Step == 2 && counter->Label == "from scene");
	FP_EXPECT(&counter->GetWorld() == world.Get() && counter->GetEntity() == e);

	world->Update(0.016f);
	world->Update(0.016f);
	FP_EXPECT(FirstCounter(*world.Get(), e)->Count == 4);

	// A script added while playing starts on the next frame.
	Entity late = world->CreateEntity("Late");
	world->Add<ScriptComponent>(late).Scripts.Emplace("TestCounter");
	world->Update(0.016f);
	FP_EXPECT(TestCounter::Starts == 2);

	// Destroying the entity stops and frees its scripts.
	world->DestroyEntity(late);
	world->Update(0.016f);
	FP_EXPECT(TestCounter::Stops == 1);
	FP_EXPECT(TestCounter::Alive == 1);

	world->EndPlay();
	FP_EXPECT(TestCounter::Stops == 2);
	FP_EXPECT(TestCounter::Alive == 0);
	FP_EXPECT(world->Get<ScriptComponent>(e).Scripts[0].Instance == nullptr);
}

FP_TEST(Script_ReloadKeepsFieldValues)
{
	Engine::RegisterEngineComponents();
	ResetCounters();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Scripted");
	world->Add<ScriptComponent>(e).Scripts.Emplace("TestCounter", "Step 5\n");
	ScriptSystem* system = world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	world->Update(0.016f);
	FP_EXPECT(FirstCounter(*world.Get(), e)->Count == 5);

	// What a hot reload does to every running script.
	system->SuspendInstances();
	FP_EXPECT(TestCounter::Alive == 0);
	system->ResumeInstances();
	FP_EXPECT(TestCounter::Reloads == 1);
	FP_EXPECT(TestCounter::Starts == 1);
	FP_EXPECT(FirstCounter(*world.Get(), e)->Count == 5);
	world->Update(0.016f);
	FP_EXPECT(FirstCounter(*world.Get(), e)->Count == 10);
	world->EndPlay();
}

FP_TEST(Script_MissingScriptWarnsOnce)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Broken");
	world->Add<ScriptComponent>(e).Scripts.Emplace("DoesNotExist");
	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	world->Update(0.016f);
	FP_EXPECT(world->Get<ScriptComponent>(e).Scripts[0].Failed);
	world->EndPlay();
	FP_EXPECT(!world->Get<ScriptComponent>(e).Scripts[0].Failed);
}
