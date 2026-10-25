#include "Engine/UI/UIRuntime.h"
#include "Engine/Paths.h"

#if FP_WITH_ULTRALIGHT

#include <AppCore/Platform.h>
#include <Ultralight/JS.h>
#include <Ultralight/Ultralight.h>

#include <SDL3/SDL_filesystem.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace FP
{
	namespace
	{
		std::string ToStd(const ultralight::String& text) { return text.utf8().data(); }
		ultralight::String ToUL(std::string_view text) { return ultralight::String(text.data(), text.size()); }

		// Runs before the page's own scripts, so engine is always there. __fp holds the native
		// calls. engine is plain JavaScript on top, since the native object's on, off and once
		// belong to Ultralight's own event system.
		const char* const Bridge = R"js(
(function () {
	if (!window.__fp || window.engine) return;
	const handlers = {};
	const watchers = {};
	const engine = window.engine = {};
	engine.state = {};
	engine.send = function (name, data) {
		__fp.send(String(name), JSON.stringify(data === undefined ? null : data));
	};
	engine.on = function (name, fn) { (handlers[name] = handlers[name] || []).push(fn); };
	engine.watch = function (key, fn) {
		(watchers[key] = watchers[key] || []).push(fn);
		if (key in engine.state) fn(engine.state[key]);
	};
	function show(element, value) {
		if (element.tagName === 'INPUT' || element.tagName === 'PROGRESS' || element.tagName === 'METER' || element.tagName === 'SELECT' || element.tagName === 'TEXTAREA')
			element.value = value;
		else
			element.textContent = value;
	}
	function apply(key) {
		document.querySelectorAll('[data-bind="' + key + '"]').forEach(function (element) { show(element, engine.state[key]); });
	}
	engine._set = function (key, value) {
		engine.state[key] = value;
		apply(key);
		(watchers[key] || []).forEach(function (fn) { fn(value); });
	};
	engine._receive = function (name, data) {
		(handlers[name] || []).forEach(function (fn) { fn(data); });
	};
	// Elements written before the state arrived catch up once the page is built.
	document.addEventListener('DOMContentLoaded', function () { Object.keys(engine.state).forEach(apply); });
})();
)js";

		std::map<std::string, std::string>& VirtualFiles()
		{
			static std::map<std::string, std::string> files;
			return files;
		}

		std::string Normalize(const ultralight::String& path)
		{
			std::string relative = ToStd(path);
			while (!relative.empty() && (relative[0] == '/' || relative[0] == '\\'))
				relative.erase(0, 1);
			return relative;
		}

		// Pages come from the project's Content. The library's own data files, like the ICU
		// tables and certificates, come from the folder copied next to the executable.
		class ContentFileSystem final : public ultralight::FileSystem
		{
		public:
			bool FileExists(const ultralight::String& path) override
			{
				if (VirtualFiles().count(Normalize(path)))
					return true;
				std::error_code ec;
				return std::filesystem::is_regular_file(Resolve(path), ec);
			}

			ultralight::String GetFileMimeType(const ultralight::String& path) override
			{
				static const std::map<std::string, const char*> types = {
					{ ".html", "text/html" }, { ".htm", "text/html" }, { ".css", "text/css" },
					{ ".js", "application/javascript" }, { ".mjs", "application/javascript" }, { ".json", "application/json" },
					{ ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
					{ ".svg", "image/svg+xml" }, { ".webp", "image/webp" }, { ".ttf", "font/ttf" }, { ".otf", "font/otf" },
					{ ".woff", "font/woff" }, { ".woff2", "font/woff2" }, { ".txt", "text/plain" }, { ".pem", "text/plain" },
				};
				std::string extension = Resolve(path).extension().string();
				for (char& c : extension)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				const auto found = types.find(extension);
				return found != types.end() ? found->second : "application/octet-stream";
			}

			ultralight::String GetFileCharset(const ultralight::String&) override { return "utf-8"; }

			ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path) override
			{
				const auto found = VirtualFiles().find(Normalize(path));
				if (found != VirtualFiles().end())
					return ultralight::Buffer::CreateFromCopy(found->second.data(), found->second.size());
				std::ifstream file(Resolve(path), std::ios::binary);
				if (!file)
					return nullptr;
				std::ostringstream data;
				data << file.rdbuf();
				const std::string bytes = data.str();
				return ultralight::Buffer::CreateFromCopy(bytes.data(), bytes.size());
			}

		private:
			static std::filesystem::path Resolve(const ultralight::String& path)
			{
				const std::string relative = Normalize(path);
				constexpr std::string_view resources = "resources/";
				if (relative.compare(0, resources.size(), resources) == 0)
				{
					const char* base = SDL_GetBasePath();
					return std::filesystem::path(base ? base : "") / "UltralightResources" / relative.substr(resources.size());
				}
				return std::filesystem::path(Paths::GetContentDir().CStr()) / relative;
			}
		};

		class EngineLogger final : public ultralight::Logger
		{
		public:
			void LogMessage(ultralight::LogLevel level, const ultralight::String& message) override
			{
				if (level == ultralight::LogLevel::Error)
					FP_LOG_ERROR("[UI] %s", message.utf8().data());
				else if (level == ultralight::LogLevel::Warning)
					FP_LOG_WARN("[UI] %s", message.utf8().data());
				else
					FP_LOG_VERBOSE("[UI] %s", message.utf8().data());
			}
		};

		struct Global
		{
			bool Tried = false;
			ultralight::RefPtr<ultralight::Renderer> Renderer;
			std::unique_ptr<ContentFileSystem> FileSystem;
			std::unique_ptr<EngineLogger> Logger;
			std::vector<UIDocumentInstance::Impl*> Documents;
		};

		Global& GetGlobal()
		{
			static Global global;
			return global;
		}

		// SDL scancodes the page cares about, with their Windows style key codes.
		struct KeyMapping
		{
			SDL_Scancode Scancode;
			int VirtualKey;
		};

		const std::vector<KeyMapping>& KeyMappings()
		{
			static const std::vector<KeyMapping> keys = [] {
				std::vector<KeyMapping> list = {
					{ SDL_SCANCODE_RETURN, ultralight::KeyCodes::GK_RETURN }, { SDL_SCANCODE_KP_ENTER, ultralight::KeyCodes::GK_RETURN },
					{ SDL_SCANCODE_ESCAPE, ultralight::KeyCodes::GK_ESCAPE }, { SDL_SCANCODE_BACKSPACE, ultralight::KeyCodes::GK_BACK },
					{ SDL_SCANCODE_TAB, ultralight::KeyCodes::GK_TAB }, { SDL_SCANCODE_SPACE, ultralight::KeyCodes::GK_SPACE },
					{ SDL_SCANCODE_LEFT, ultralight::KeyCodes::GK_LEFT }, { SDL_SCANCODE_RIGHT, ultralight::KeyCodes::GK_RIGHT },
					{ SDL_SCANCODE_UP, ultralight::KeyCodes::GK_UP }, { SDL_SCANCODE_DOWN, ultralight::KeyCodes::GK_DOWN },
					{ SDL_SCANCODE_DELETE, ultralight::KeyCodes::GK_DELETE }, { SDL_SCANCODE_HOME, ultralight::KeyCodes::GK_HOME },
					{ SDL_SCANCODE_END, ultralight::KeyCodes::GK_END }, { SDL_SCANCODE_PAGEUP, ultralight::KeyCodes::GK_PRIOR },
					{ SDL_SCANCODE_PAGEDOWN, ultralight::KeyCodes::GK_NEXT },
				};
				for (int i = 0; i < 26; ++i)
					list.push_back({ static_cast<SDL_Scancode>(SDL_SCANCODE_A + i), 'A' + i });
				// SDL lists 1 to 9 then 0.
				for (int i = 0; i < 10; ++i)
					list.push_back({ static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i), i == 9 ? '0' : '1' + i });
				return list;
			}();
			return keys;
		}
	}

	struct UIDocumentInstance::Impl final : public ultralight::LoadListener, public ultralight::ViewListener
	{
		ultralight::RefPtr<ultralight::View> View;
		ultralight::js::API Api{ "__fp" };
		String Page;
		String LoadedPath;
		bool Ready = false;
		uint64 Version = 0;
		float Scale = 1;
		Vec2 LastMouse{ -1, -1 };
		bool MouseDown[3] = {};
		Array<UIMessage> Messages;
		// Values and events from before the page was ready, replayed once it is.
		std::map<std::string, std::string> State;
		std::vector<std::pair<std::string, std::string>> PendingEvents;

		void OnWindowObjectReady(ultralight::View* caller, uint64_t, bool isMainFrame, const ultralight::String&) override
		{
			if (isMainFrame)
				caller->EvaluateScript(Bridge);
		}

		void OnDOMReady(ultralight::View*, uint64_t, bool isMainFrame, const ultralight::String&) override
		{
			if (!isMainFrame)
				return;
			Ready = true;
			for (const auto& [key, json] : State)
				Run("engine._set(" + UIDocument::ToJsonString(key) + "," + json + ")");
			for (const auto& [name, json] : PendingEvents)
				Run("engine._receive(" + UIDocument::ToJsonString(name) + "," + json + ")");
			PendingEvents.clear();
		}

		void OnFailLoading(ultralight::View*, uint64_t, bool isMainFrame, const ultralight::String& url, const ultralight::String& description,
			const ultralight::String&, int) override
		{
			if (isMainFrame)
				FP_LOG_ERROR("[UI] Could not load %s: %s", url.utf8().data(), description.utf8().data());
		}

		void OnAddConsoleMessage(ultralight::View*, const ultralight::ConsoleMessage& message) override
		{
			const std::string text = ToStd(message.message());
			const std::string source = ToStd(message.source_id());
			if (message.level() == ultralight::kMessageLevel_Error)
				FP_LOG_ERROR("[UI] %s (%s:%u)", text.c_str(), source.c_str(), message.line_number());
			else if (message.level() == ultralight::kMessageLevel_Warning)
				FP_LOG_WARN("[UI] %s (%s:%u)", text.c_str(), source.c_str(), message.line_number());
			else
				FP_LOG("[UI] %s", text.c_str());
		}

		std::string Run(const std::string& script)
		{
			if (!View)
				return {};
			ultralight::String exception;
			const std::string result = ToStd(View->EvaluateScript(ToUL(script), &exception));
			if (!exception.empty())
				FP_LOG_ERROR("[UI] %s", exception.utf8().data());
			return result;
		}

		void Fire(const ultralight::MouseEvent::Type type, ultralight::MouseEvent::Button button, Vec2 position)
		{
			ultralight::MouseEvent event;
			event.type = type;
			event.x = static_cast<int>(position.X);
			event.y = static_cast<int>(position.Y);
			event.button = button;
			View->FireMouseEvent(event);
		}
	};

	UIDocumentInstance::UIDocumentInstance() : m_Impl(std::make_unique<Impl>())
	{
		if (!UIRuntime::Get().IsAvailable())
			return;
		ultralight::ViewConfig config;
		config.is_accelerated = false;
		config.is_transparent = true;
		config.initial_focus = true;
		m_Impl->View = GetGlobal().Renderer->CreateView(64, 64, config, nullptr);
		m_Impl->View->set_load_listener(m_Impl.get());
		m_Impl->View->set_view_listener(m_Impl.get());
		Impl* impl = m_Impl.get();
		m_Impl->Api.Bind("send", [impl](std::string name, std::string data) {
			impl->Messages.Add({ String(name), String(data) });
		});
		if (!m_Impl->Api.AttachTo(m_Impl->View.get()))
			FP_LOG_ERROR("[UI] Could not give the page its engine object");
		GetGlobal().Documents.push_back(m_Impl.get());
	}

	UIDocumentInstance::~UIDocumentInstance()
	{
		std::vector<Impl*>& documents = GetGlobal().Documents;
		std::erase(documents, m_Impl.get());
		if (m_Impl->View)
		{
			m_Impl->Api.DetachFrom(m_Impl->View.get());
			m_Impl->View->set_load_listener(nullptr);
			m_Impl->View->set_view_listener(nullptr);
		}
	}

	void UIDocumentInstance::SetJson(std::string_view key, std::string_view json)
	{
		m_Impl->State[std::string(key)] = std::string(json);
		if (m_Impl->Ready)
			m_Impl->Run("engine._set(" + ToJsonString(key) + "," + std::string(json) + ")");
	}

	void UIDocumentInstance::Emit(std::string_view name, std::string_view json)
	{
		if (m_Impl->Ready)
			m_Impl->Run("engine._receive(" + ToJsonString(name) + "," + std::string(json) + ")");
		else
			m_Impl->PendingEvents.push_back({ std::string(name), std::string(json) });
	}

	String UIDocumentInstance::RunScript(std::string_view script)
	{
		return String(m_Impl->Run(std::string(script)));
	}

	void UIDocumentInstance::LoadPage(std::string_view path)
	{
		m_Impl->Page = String(path);
		m_Impl->Ready = false;
		if (m_Impl->View && !path.empty())
		{
			const String resolved = Paths::AssetPath(path);
			if (resolved == m_Impl->LoadedPath)
				m_Impl->View->Reload();
			else
				m_Impl->View->LoadURL(ToUL("file:///" + std::string(resolved.View())));
			m_Impl->LoadedPath = resolved;
		}
	}

	bool UIDocumentInstance::IsReady() const { return m_Impl->Ready; }
	const String& UIDocumentInstance::GetPage() const { return m_Impl->Page; }
	uint32 UIDocumentInstance::GetWidth() const { return m_Impl->View ? m_Impl->View->width() : 0; }
	uint32 UIDocumentInstance::GetHeight() const { return m_Impl->View ? m_Impl->View->height() : 0; }
	uint64 UIDocumentInstance::GetVersion() const { return m_Impl->Version; }

	void UIDocumentInstance::Resize(uint32 width, uint32 height, float scale)
	{
		if (!m_Impl->View || width == 0 || height == 0)
			return;
		if (width == m_Impl->View->width() && height == m_Impl->View->height() && scale == m_Impl->Scale)
			return;
		m_Impl->Scale = scale > 0 ? scale : 1;
		m_Impl->View->Resize(width, height, m_Impl->Scale);
	}

	void UIDocumentInstance::ReadPixels(const std::function<void(const void*, uint32, uint32, uint32)>& read)
	{
		ultralight::Surface* surface = m_Impl->View ? m_Impl->View->surface() : nullptr;
		if (!surface)
			return;
		void* pixels = surface->LockPixels();
		if (pixels)
			read(pixels, surface->width(), surface->height(), surface->row_bytes());
		surface->UnlockPixels();
	}

	void UIDocumentInstance::SendInput()
	{
		Impl& impl = *m_Impl;
		if (!impl.View)
			return;
		// The page lays out in CSS pixels, which are Scale screen pixels each.
		const Vec2 mouse = Input::GetMousePosition() / impl.Scale;
		const MouseButton buttons[3] = { MouseButton::Left, MouseButton::Middle, MouseButton::Right };
		const ultralight::MouseEvent::Button pageButtons[3] = { ultralight::MouseEvent::kButton_Left, ultralight::MouseEvent::kButton_Middle, ultralight::MouseEvent::kButton_Right };
		ultralight::MouseEvent::Button held = ultralight::MouseEvent::kButton_None;
		for (int i = 0; i < 3; ++i)
		{
			if (impl.MouseDown[i])
				held = pageButtons[i];
		}
		if (mouse.X != impl.LastMouse.X || mouse.Y != impl.LastMouse.Y)
		{
			impl.Fire(ultralight::MouseEvent::kType_MouseMoved, held, mouse);
			impl.LastMouse = mouse;
		}
		for (int i = 0; i < 3; ++i)
		{
			const bool down = Input::IsMouseDown(buttons[i]);
			if (down != impl.MouseDown[i])
				impl.Fire(down ? ultralight::MouseEvent::kType_MouseDown : ultralight::MouseEvent::kType_MouseUp, pageButtons[i], mouse);
			impl.MouseDown[i] = down;
		}

		const Vec2 scroll = Input::GetScroll();
		if (scroll.X != 0 || scroll.Y != 0)
		{
			ultralight::ScrollEvent event;
			event.type = ultralight::ScrollEvent::kType_ScrollByPixel;
			event.delta_x = static_cast<int>(scroll.X * 40);
			event.delta_y = static_cast<int>(scroll.Y * 40);
			impl.View->FireScrollEvent(event);
		}

		unsigned modifiers = 0;
		if (Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT)) modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
		if (Input::IsKeyDown(SDL_SCANCODE_LCTRL) || Input::IsKeyDown(SDL_SCANCODE_RCTRL)) modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
		if (Input::IsKeyDown(SDL_SCANCODE_LALT) || Input::IsKeyDown(SDL_SCANCODE_RALT)) modifiers |= ultralight::KeyEvent::kMod_AltKey;
		if (Input::IsKeyDown(SDL_SCANCODE_LGUI) || Input::IsKeyDown(SDL_SCANCODE_RGUI)) modifiers |= ultralight::KeyEvent::kMod_MetaKey;
		for (const KeyMapping& key : KeyMappings())
		{
			const bool pressed = Input::WasKeyPressed(key.Scancode);
			if (!pressed && !Input::WasKeyReleased(key.Scancode))
				continue;
			ultralight::KeyEvent event;
			event.type = pressed ? ultralight::KeyEvent::kType_RawKeyDown : ultralight::KeyEvent::kType_KeyUp;
			event.virtual_key_code = key.VirtualKey;
			event.native_key_code = 0;
			event.modifiers = modifiers;
			ultralight::GetKeyIdentifierFromVirtualKeyCode(event.virtual_key_code, event.key_identifier);
			impl.View->FireKeyEvent(event);
			// Enter and Tab also type a character, the way a browser does.
			if (pressed && (key.VirtualKey == ultralight::KeyCodes::GK_RETURN || key.VirtualKey == ultralight::KeyCodes::GK_TAB))
			{
				ultralight::KeyEvent typed;
				typed.type = ultralight::KeyEvent::kType_Char;
				typed.text = key.VirtualKey == ultralight::KeyCodes::GK_RETURN ? "\r" : "\t";
				typed.unmodified_text = typed.text;
				typed.modifiers = modifiers;
				impl.View->FireKeyEvent(typed);
			}
		}

		const std::string_view text = Input::GetTypedText();
		if (!text.empty())
		{
			ultralight::KeyEvent typed;
			typed.type = ultralight::KeyEvent::kType_Char;
			typed.text = ToUL(text);
			typed.unmodified_text = typed.text;
			typed.modifiers = modifiers;
			impl.View->FireKeyEvent(typed);
		}
	}

	void UIDocumentInstance::TakeMessages(Array<UIMessage>& out)
	{
		for (UIMessage& message : m_Impl->Messages)
			out.Add(std::move(message));
		m_Impl->Messages.Clear();
	}

	UIRuntime& UIRuntime::Get()
	{
		static UIRuntime runtime;
		return runtime;
	}

	bool UIRuntime::IsAvailable()
	{
		Global& global = GetGlobal();
		if (global.Tried)
			return global.Renderer.get() != nullptr;
		global.Tried = true;

		global.FileSystem = std::make_unique<ContentFileSystem>();
		global.Logger = std::make_unique<EngineLogger>();
		ultralight::Config config;
		ultralight::Platform& platform = ultralight::Platform::instance();
		platform.set_config(config);
		platform.set_font_loader(ultralight::GetPlatformFontLoader());
		platform.set_file_system(global.FileSystem.get());
		platform.set_logger(global.Logger.get());
		global.Renderer = ultralight::Renderer::Create();
		if (!global.Renderer)
			FP_LOG_ERROR("[UI] Ultralight did not start, so game UI is off");
		return global.Renderer.get() != nullptr;
	}

	void UIRuntime::Update()
	{
		Global& global = GetGlobal();
		if (!global.Renderer)
			return;
		global.Renderer->Update();
		global.Renderer->RefreshDisplay(0);
		global.Renderer->Render();
		// A page that drew anything gets a new version for the renderer to upload.
		for (UIDocumentInstance::Impl* document : global.Documents)
		{
			ultralight::Surface* surface = document->View ? document->View->surface() : nullptr;
			if (surface && !surface->dirty_bounds().IsEmpty())
			{
				++document->Version;
				surface->ClearDirtyBounds();
			}
		}
	}

	void UIRuntime::Shutdown()
	{
		Global& global = GetGlobal();
		global.Documents.clear();
		global.Renderer = nullptr;
	}

	void UIRuntime::SetVirtualFile(std::string_view path, std::string_view text)
	{
		while (!path.empty() && path[0] == '/')
			path.remove_prefix(1);
		VirtualFiles()[std::string(path)] = std::string(text);
	}
}

#else

// Built without the Ultralight SDK. Pages never load and nothing draws.
namespace FP
{
	struct UIDocumentInstance::Impl
	{
		String Page;
	};

	UIDocumentInstance::UIDocumentInstance() : m_Impl(std::make_unique<Impl>()) {}
	UIDocumentInstance::~UIDocumentInstance() = default;
	void UIDocumentInstance::SetJson(std::string_view, std::string_view) {}
	void UIDocumentInstance::Emit(std::string_view, std::string_view) {}
	String UIDocumentInstance::RunScript(std::string_view) { return {}; }
	void UIDocumentInstance::LoadPage(std::string_view path) { m_Impl->Page = String(path); }
	bool UIDocumentInstance::IsReady() const { return false; }
	const String& UIDocumentInstance::GetPage() const { return m_Impl->Page; }
	uint32 UIDocumentInstance::GetWidth() const { return 0; }
	uint32 UIDocumentInstance::GetHeight() const { return 0; }
	uint64 UIDocumentInstance::GetVersion() const { return 0; }
	void UIDocumentInstance::Resize(uint32, uint32, float) {}
	void UIDocumentInstance::ReadPixels(const std::function<void(const void*, uint32, uint32, uint32)>&) {}
	void UIDocumentInstance::SendInput() {}
	void UIDocumentInstance::TakeMessages(Array<UIMessage>&) {}

	UIRuntime& UIRuntime::Get()
	{
		static UIRuntime runtime;
		return runtime;
	}

	bool UIRuntime::IsAvailable()
	{
		static bool warned = false;
		if (!warned)
			FP_LOG_WARN("Built without the Ultralight SDK, so game UI is off. Run Scripts/GetUltralight and rebuild.");
		warned = true;
		return false;
	}

	void UIRuntime::Update() {}
	void UIRuntime::Shutdown() {}
	void UIRuntime::SetVirtualFile(std::string_view, std::string_view) {}
}

#endif
