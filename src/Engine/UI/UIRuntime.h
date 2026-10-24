#pragma once

#include "Engine/UI/UIDocument.h"
#include "Engine/Input.h"

#include <functional>
#include <memory>

namespace FP
{
	// A message a page sent with engine.send(name, data). Data is JSON.
	struct UIMessage
	{
		String Name;
		String Data;
	};

	// The engine's side of a UI page: an Ultralight view drawn into pixels on the CPU.
	class UIDocumentInstance final : public UIDocument
	{
	public:
		UIDocumentInstance();
		~UIDocumentInstance() override;

		void SetJson(std::string_view key, std::string_view json) override;
		void Emit(std::string_view name, std::string_view json) override;
		String RunScript(std::string_view script) override;
		void LoadPage(std::string_view path) override;
		bool IsReady() const override;

		// The page in Content last asked to load, or empty.
		const String& GetPage() const;

		// Size in pixels. scale is pixels per CSS pixel, so pages look the same on any screen.
		void Resize(uint32 width, uint32 height, float scale);
		uint32 GetWidth() const;
		uint32 GetHeight() const;

		// Goes up whenever the page draws something new, so the renderer only uploads changes.
		uint64 GetVersion() const;

		// Calls read with the page's pixels as premultiplied BGRA rows, locked for the call.
		// Not called when the page has no pixels yet.
		void ReadPixels(const std::function<void(const void* pixels, uint32 width, uint32 height, uint32 rowBytes)>& read);

		// Feeds this frame's Input to the page. Mouse positions are in pixels.
		void SendInput();

		// Takes the messages the page sent since the last call.
		void TakeMessages(Array<UIMessage>& out);

		// The Ultralight side, kept out of this header so game code never needs the SDK.
		struct Impl;

	private:
		std::unique_ptr<Impl> m_Impl;
	};

	// Starts Ultralight on first use and steps every page once a frame. Host only.
	class UIRuntime
	{
	public:
		static UIRuntime& Get();

		// False when the engine was built without the Ultralight SDK, or it failed to start.
		bool IsAvailable();

		// Lays out, runs timers and redraws every page that changed.
		void Update();

		// Frees every page and the renderer.
		void Shutdown();

		// Serves text at a path as if it were a file in Content, for pages made in code and tests.
		void SetVirtualFile(std::string_view path, std::string_view text);
	};
}
