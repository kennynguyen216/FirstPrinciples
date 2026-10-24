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
