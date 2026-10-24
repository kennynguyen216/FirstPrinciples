#include "Tests/TestFramework.h"
#include "Engine/Paths.h"
#include "Engine/UI/UIRuntime.h"

#include <chrono>
#include <thread>

using namespace FP;

namespace
{
	// Steps the UI until done says so, or a few seconds pass.
	template <typename Done>
	bool Pump(Done done)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline)
		{
			UIRuntime::Get().Update();
			if (done())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}
}

FP_TEST(UI_PagesRenderBindAndTalk)
{
	if (!UIRuntime::Get().IsAvailable())
		return; // built without Ultralight

	// Served from memory so tests never touch Content.
	UIRuntime::Get().SetVirtualFile("Tests/UITest.html", R"html(<!DOCTYPE html>
<html><body style="margin:0">
<div style="width:40px;height:40px;background:rgb(255,0,0)"></div>
<span id="score" data-bind="score"></span>
<script>
engine.on("ping", function (data) { engine.send("pong", { got: data.value + 1 }); });
engine.watch("score", function (value) { document.title = "score " + value; });
</script>
</body></html>)html");

	UIDocumentInstance document;
	document.Resize(100, 60, 1);
	// Values set before the page is ready still reach it.
	document.Set("score", 41);
	document.LoadPage("Tests/UITest.html");
	FP_EXPECT(Pump([&] { return document.IsReady() && document.GetVersion() > 0; }));
	FP_EXPECT(document.RunScript("document.getElementById('score').textContent") == "41");
	FP_EXPECT(document.RunScript("document.title") == "score 41");

	// C++ to page and back.
	document.Emit("ping", "{\"value\":1}");
	Array<UIMessage> messages;
	FP_EXPECT(Pump([&] { document.TakeMessages(messages); return !messages.IsEmpty(); }));
	FP_EXPECT(messages.Num() == 1 && messages[0].Name == "pong" && messages[0].Data == "{\"got\":2}");

	// The red box is opaque red and the rest of the page is see through.
	bool checked = false;
	document.ReadPixels([&](const void* pixels, uint32 width, uint32 height, uint32 rowBytes) {
		const uint8* bytes = static_cast<const uint8*>(pixels);
		const uint8* inside = bytes + 10 * rowBytes + 10 * 4;
		const uint8* outside = bytes + 50 * rowBytes + 90 * 4;
		FP_EXPECT(width == 100 && height == 60);
		FP_EXPECT(inside[2] == 255 && inside[1] == 0 && inside[0] == 0 && inside[3] == 255);
		FP_EXPECT(outside[3] == 0);
		checked = true;
	});
	FP_EXPECT(checked);
}

#include "Engine/Engine.h"
#include "Engine/UI/UISystem.h"

FP_TEST(UI_SystemLoadsPagesForComponents)
{
	Engine::RegisterEngineComponents();
	UIRuntime::Get().SetVirtualFile("Tests/Hud.html", "<html><body><b data-bind=\"lives\">?</b></body></html>");
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity hud = world->CreateEntity("Hud");
	world->Add<UIComponent>(hud).Page = "Tests/Hud.html";
	const Entity plain = world->CreateEntity("Plain");
	world->AddSystem<UISystem>();
	world->BeginPlay();
	world->Update(0.016f);

	UIDocumentInstance* document = UISystem::For(*world.Get(), hud);
	FP_EXPECT(document && document->GetPage() == "Tests/Hud.html");
	FP_EXPECT(!UISystem::For(*world.Get(), plain));

	// Pages appear once the runtime has steps to load them, when Ultralight is built in.
	if (UIRuntime::Get().IsAvailable())
	{
		document->Set("lives", 3);
		FP_EXPECT(Pump([&] { world->Update(0.016f); return document->IsReady(); }));
		FP_EXPECT(document->RunScript("document.querySelector('b').textContent") == "3");
	}

	// Stopping drops every page.
	world->EndPlay();
	FP_EXPECT(!world->Has<UIStateComponent>(hud));
}

#include "Engine/Scripting/Script.h"
#include "Engine/Scripting/ScriptSystem.h"

namespace
{
	// Talks to its entity's page the way a HUD script would.
	class TestUIListener : public Script
	{
	public:
		String LastEvent;
		double LastValue = 0;

		void OnStart() override
		{
			if (UIDocument* ui = GetUI())
				ui->Set("greeting", "hello");
		}

		void OnUIEvent(std::string_view name, const Json& data) override
		{
			LastEvent = String(name);
			LastValue = data["n"].AsNumber();
		}
	};
}

FP_SCRIPT_FIELDS(TestUIListener) {}

FP_TEST(UI_PagesTalkToScripts)
{
	if (!UIRuntime::Get().IsAvailable())
		return;
	Engine::RegisterEngineComponents();
	UIRuntime::Get().SetVirtualFile("Tests/Talk.html", R"html(<html><body><i data-bind="greeting"></i>
<script>engine.watch("greeting", function (text) { engine.send("greeted", { n: text.length }); });</script></body></html>)html");
	StrongObjectPtr<World> world(NewObject<World>());
	const Entity hud = world->CreateEntity("Hud");
	world->Add<UIComponent>(hud).Page = "Tests/Talk.html";
	world->Add<ScriptComponent>(hud).Scripts.Emplace("TestUIListener");
	world->AddSystem<ScriptSystem>();
	world->AddSystem<UISystem>();
	world->BeginPlay();

	TestUIListener* listener = static_cast<TestUIListener*>(world->Get<ScriptComponent>(hud).Scripts[0].Instance);
	FP_EXPECT(listener);
	if (!listener)
		return;
	// The value set in OnStart reaches the page, which answers through engine.send.
	FP_EXPECT(Pump([&] { world->Update(0.016f); return listener->LastEvent == "greeted"; }));
	FP_EXPECT(listener->LastValue == 5);
	world->EndPlay();
}
