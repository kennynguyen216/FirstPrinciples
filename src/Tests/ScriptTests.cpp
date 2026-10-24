#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptRuntime.h"
#include "Engine/Scripting/ScriptSystem.h"
#include "Engine/SceneHistory.h"
#include "Engine/FieldSerializer.h"

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

FP_SCRIPT_FIELDS(TestCounter)
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

FP_TEST(Script_AudioAndPropertiesSurviveHistoryAndRejectedLoad)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity entity = world->CreateEntity("Scripted audio");
	const Guid id = world->Get<IdComponent>(entity).Id;
	const String originalProperties = "Label  \"has \\\"quotes\\\" and space\"\nSpeed\t7\n";
	world->Add<ScriptComponent>(entity).Scripts.Emplace("NotCompiledYet", originalProperties);
	world->Add<AudioSourceComponent>(entity).Clip = "Audio/Click.wav";
	SceneHistory history(*world.Get());
	history.Reset(id);
	const String original = SceneSerializer::SaveToString(*world.Get());
	world->Get<ScriptComponent>(entity).Scripts[0].Properties = "Speed 9\n";
	world->Get<AudioSourceComponent>(entity).Volume = 0.25f;
	FP_EXPECT(history.Commit(id));
	const String edited = SceneSerializer::SaveToString(*world.Get());
	FP_EXPECT(history.Undo());
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == original);
	FP_EXPECT(world->Get<ScriptComponent>(world->FindEntityByGuid(id)).Scripts[0].Properties == originalProperties);
	FP_EXPECT(history.Redo());
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == edited);
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), (edited + "  Broken \"unterminated\n").View()));
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == edited);
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), (edited + "entity invalid\n").View()));
	FP_EXPECT(SceneSerializer::SaveToString(*world.Get()) == edited);
}

FP_TEST(FieldSerializer_RawLineCallbacksKeepParseFailures)
{
	int twoArgumentCalls = 0;
	FP_EXPECT(FieldSerializer::ForEachLine("Value 1\n", [&](int32 line, const Array<String>& tokens) {
		++twoArgumentCalls;
		return line == 1 && tokens.Num() == 2;
	}));
	FP_EXPECT(twoArgumentCalls == 1);
	String raw;
	FP_EXPECT(FieldSerializer::ForEachLine("# skipped\n  Label  \"two words\"\n", [&](int32 line, const Array<String>& tokens, std::string_view text) {
		raw = text;
		return line == 2 && tokens[1] == "two words";
	}));
	FP_EXPECT(raw == "  Label  \"two words\"");
	FP_EXPECT(!FieldSerializer::ForEachLine("Value 1\n", [](int32, const Array<String>&, std::string_view) { return false; }));
	FP_EXPECT(!FieldSerializer::ForEachLine("Label \"unterminated\n", [](int32, const Array<String>&) { return true; }));
}

FP_TEST(Script_SceneReplacementKeepsEntityCleanup)
{
	Engine::RegisterEngineComponents();
	ResetCounters();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity original = world->CreateEntity("Original");
	world->Add<ScriptComponent>(original).Scripts.Emplace("TestCounter");
	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	FP_EXPECT(TestCounter::Alive == 1);
	StrongObjectPtr<World> replacement(NewObject<World>());
	const Entity incoming = replacement->CreateEntity("Replacement");
	replacement->Add<ScriptComponent>(incoming).Scripts.Emplace("TestCounter");
	const String text = SceneSerializer::SaveToString(*replacement.Get());
	FP_EXPECT(!SceneSerializer::LoadFromString(*world.Get(), (text + "entity invalid\n").View()));
	FP_EXPECT(TestCounter::Alive == 1 && TestCounter::Stops == 0);
	FP_EXPECT(SceneSerializer::LoadFromString(*world.Get(), text.View()));
	FP_EXPECT(TestCounter::Starts == 2 && TestCounter::Stops == 1 && TestCounter::Alive == 1);
	FP_EXPECT(world->IsPlaying());
	world->Update(0.016f);
	FP_EXPECT(TestCounter::Starts == 2 && TestCounter::Alive == 1);
	world->DestroyEntity(world->FindEntityByName("Replacement"));
	world->Update(0.016f);
	FP_EXPECT(TestCounter::Alive == 0 && TestCounter::Stops == 2);
	world->EndPlay();
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

FP_TEST(Script_LoadsGameScriptsModule)
{
	Engine::RegisterEngineComponents();
	ScriptRuntime& runtime = ScriptRuntime::Get();
	if (!runtime.LoadModule(ScriptRuntime::GetDefaultModulePath()))
	{
		FP_LOG_WARN("Skipping, GameScripts is not built");
		return;
	}
	FP_EXPECT(runtime.FindType("Rotator") != nullptr);
	FP_EXPECT(runtime.FindType("TestCounter") != nullptr);

	// A script from the module edits a world owned by the host.
	StrongObjectPtr<World> world(NewObject<World>());
	Entity e = world->CreateEntity("Spinning");
	world->Add<ScriptComponent>(e).Scripts.Emplace("Rotator", "DegreesPerSecond 0 90 0\n");
	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	world->Update(1.0f);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(e).Rotation.Y - 90) < 1e-3f);

	// Reloading mid play keeps the script and its values.
	FP_EXPECT(runtime.Reload());
	world->Update(1.0f);
	FP_EXPECT(std::fabs(world->Get<TransformComponent>(e).Rotation.Y - 180) < 1e-3f);

	world->EndPlay();
	runtime.UnloadModule();
	FP_EXPECT(runtime.FindType("Rotator") == nullptr);
}

namespace
{
	// Builds a small state machine, the way game scripts drive their animators.
	class TestAnimatorScript : public Script
	{
	public:
		float Speed = 3;

		void OnStart() override
		{
			Animator* animator = GetAnimator();
			if (!animator)
				return;
			animator->AddState("Idle", "IdleClip");
			animator->AddState("Run", "RunClip");
			animator->AddTransition("Idle", "Run").When("Speed", AnimCompare::Greater, 0.5f);
			animator->AddTransition("Run", "Idle").When("Speed", AnimCompare::Less, 0.5f);
		}

		void OnUpdate(float) override
		{
			if (Animator* animator = GetAnimator())
				animator->SetFloat("Speed", Speed);
		}
	};
}

FP_SCRIPT_FIELDS(TestAnimatorScript)
{
	fields.Field("Speed", &TestAnimatorScript::Speed);
}

FP_TEST(Script_DrivesAnimator)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity e = world->CreateEntity("Runner");
	world->Add<AnimatorComponent>(e);
	world->Add<ScriptComponent>(e).Scripts.Emplace("TestAnimatorScript");
	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	world->Update(0.016f);

	const AnimatorInstance* animator = static_cast<AnimatorInstance*>(world->GetAnimator(e));
	FP_EXPECT(animator && animator->GetMachine().GetStates().Num() == 2);
	FP_EXPECT(animator->GetMachine().GetTransitions().Num() == 2);
	FP_EXPECT(animator->GetFloat("Speed") == 3);

	// Starting again, as after a hot reload, keeps the same machine.
	world->EndPlay();
	world->BeginPlay();
	animator = static_cast<AnimatorInstance*>(world->GetAnimator(e));
	FP_EXPECT(animator->GetMachine().GetStates().Num() == 2);
	world->EndPlay();
}

// Written the way the header tool writes it, before the class is defined.
class TestReflected;
template <>
struct FP::ScriptReflection<TestReflected>
{
	static constexpr const char* Description = "Counts presses.";
	static constexpr const char* Category = "Tests";
	static constexpr const char* Callbacks = "OnStart";

	template <typename T = TestReflected>
	static void Describe(ScriptTypeBuilder<T>& fields)
	{
		fields.Field("Presses", &T::Presses).Tooltip("How many times Press ran").Range(0, 10).ReadOnly();
		fields.template Function<&T::Press>("Press", "Adds one");
	}
};

class TestReflected : public Script
{
public:
	int32 Presses = 0;
	void OnStart() override {}
	void Press() { ++Presses; }
};

FP_SCRIPT(TestReflected);

FP_TEST(Script_UsesGeneratedReflection)
{
	const ScriptType* type = ScriptRuntime::Get().FindType("TestReflected");
	FP_EXPECT(type && std::string_view(type->Description) == "Counts presses.");
	FP_EXPECT(type && std::string_view(type->Category) == "Tests" && std::string_view(type->Callbacks) == "OnStart");
	FP_EXPECT(type && type->Fields.Num() == 1);
	const FieldInfo& field = type->Fields[0];
	FP_EXPECT(std::string_view(field.Tooltip) == "How many times Press ran" && field.HasRange && field.Max == 10 && field.ReadOnly);

	// Functions call straight into the instance.
	FP_EXPECT(type && type->Functions.Num() == 1 && std::string_view(type->Functions[0].Name) == "Press");
	Script* instance = ScriptRuntime::Get().CreateInstance(*type, nullptr, NullEntity, "Presses 2\n");
	type->Functions[0].Call(instance);
	FP_EXPECT(static_cast<TestReflected*>(instance)->Presses == 3);
	ScriptRuntime::Get().DestroyInstance(*type, instance);
}

namespace
{
	// Uses the script helpers the way gameplay code would.
	class TestHelpers : public Script
	{
	public:
		int32 Ticks = 0;
		int32 Once = 0;
		Entity Spawned = NullEntity;
		Array<Entity> Prefab;

		void OnStart() override
		{
			Spawned = Spawn("Bullet", { 1, 2, 3 });
			AddChild(Spawned, false);
			Every(0.25f, [this]() { ++Ticks; });
			After(0.6f, [this]() { ++Once; });
			Prefab = SpawnScene("Scenes/Main.scene", { 100, 0, 0 });
		}
	};
}

FP_SCRIPT_FIELDS(TestHelpers) {}

FP_TEST(Script_HelpersSpawnParentAndTime)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity owner = world->CreateEntity("Owner");
	world->Get<TransformComponent>(owner).Position = { 10, 0, 0 };
	world->Add<ScriptComponent>(owner).Scripts.Emplace("TestHelpers");
	world->AddSystem<ScriptSystem>();
	world->BeginPlay();
	TestHelpers* helpers = static_cast<TestHelpers*>(world->Get<ScriptComponent>(owner).Scripts[0].Instance);
	FP_EXPECT(helpers);
	if (!helpers)
		return;

	// Spawned as a child, so its position is an offset from the owner.
	FP_EXPECT(world->GetParent(helpers->Spawned) == owner);
	FP_EXPECT((world->GetWorldPosition(helpers->Spawned) - Vec3(11, 2, 3)).Length() < 1e-4f);
	FP_EXPECT(helpers->FindChild("Bullet") == helpers->Spawned && helpers->GetChildren().Num() == 1);
	FP_EXPECT(helpers->GetScript<TestHelpers>() == helpers && helpers->FindScript<TestHelpers>() == helpers);
	FP_EXPECT(!helpers->GetScript<TestCounter>());

	// The prefab came in with new ids and its top level entities moved over.
	FP_EXPECT(!helpers->Prefab.IsEmpty());
	for (Entity entity : helpers->Prefab)
		FP_EXPECT(world->IsValid(entity));
	FP_EXPECT(world->GetWorldPosition(helpers->Prefab[0]).X >= 50);

	// Timers fire on schedule, repeating ones keep going and one shot ones stop.
	for (int i = 0; i < 10; ++i)
		world->Update(0.1f);
	FP_EXPECT(helpers->Ticks == 4 && helpers->Once == 1);

	// Destroying the owner takes its child with it.
	helpers->DestroySelf();
	const Entity bullet = helpers->Spawned;
	world->Update(0.1f);
	FP_EXPECT(!world->IsValid(owner) && !world->IsValid(bullet));
	world->EndPlay();
}

FP_TEST(Script_LoadSceneWaitsForTheFrameToEnd)
{
	Engine::RegisterEngineComponents();
	StrongObjectPtr<World> world(NewObject<World>());
	world->CreateEntity("Old");
	world->BeginPlay();
	world->RequestSceneLoad("Scenes/Main.scene");
	FP_EXPECT(world->FindEntityByName("Old") != NullEntity);
	world->Update(0.016f);
	FP_EXPECT(world->FindEntityByName("Old") == NullEntity && !world->GetEntities().IsEmpty());
	FP_EXPECT(world->IsPlaying());
	world->EndPlay();
}
