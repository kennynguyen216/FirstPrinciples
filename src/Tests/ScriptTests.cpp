#include "Tests/TestFramework.h"
#include "Engine/Engine.h"
#include "Engine/SceneSerializer.h"
#include "Engine/Scripting/ScriptComponent.h"

using namespace FP;

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
