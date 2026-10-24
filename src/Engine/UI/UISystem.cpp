#include "Engine/UI/UISystem.h"
#include "Engine/Json.h"
#include "Engine/Scripting/Script.h"
#include "Engine/Paths.h"

#include <filesystem>

namespace FP
{
	namespace
	{
		// Newest write time of the files beside a page, which covers its CSS and scripts.
		int64 FolderStamp(const String& page)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path folder = fs::path(Paths::Content(page.View()).CStr()).parent_path();
			int64 newest = 0;
			for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec))
			{
				const int64 time = static_cast<int64>(entry.last_write_time(ec).time_since_epoch().count());
				newest = time > newest ? time : newest;
			}
			return newest;
		}
	}

	UIDocumentInstance* UISystem::For(World& world, Entity entity)
	{
		const UIComponent* ui = world.IsValid(entity) ? world.TryGet<UIComponent>(entity) : nullptr;
		if (!ui)
			return nullptr;
		UIStateComponent& state = world.GetRegistry().get_or_emplace<UIStateComponent>(entity);
		if (!state.Document)
			state.Document = std::make_unique<UIDocumentInstance>();
		return state.Document.get();
	}

	void UISystem::Update(World& world, float deltaTime)
	{
		for (auto [entity, ui] : world.View<UIComponent>().each())
		{
			UIDocumentInstance* document = For(world, entity);
			UIStateComponent& state = world.Get<UIStateComponent>(entity);
			if (document->GetPage() != ui.Page.View())
			{
				document->LoadPage(ui.Page.View());
				state.FolderStamp = FolderStamp(ui.Page);
			}
			// Edits to the page, its styles or its scripts show up twice a second while playing.
			state.CheckTimer += deltaTime;
			if (state.CheckTimer > 0.5f && !ui.Page.IsEmpty())
			{
				state.CheckTimer = 0;
				const int64 stamp = FolderStamp(ui.Page);
				if (stamp != state.FolderStamp)
				{
					state.FolderStamp = stamp;
					document->LoadPage(ui.Page.View());
					FP_LOG("Reloaded %s", ui.Page.CStr());
				}
			}
			// The renderer says how big the screen is. Until it has, the page waits at its first size.
			if (state.Width > 0 && state.Height > 0)
				document->Resize(state.Width, state.Height, state.Scale);
			if (ui.Input && ui.Visible && state.Drawn)
				document->SendInput();
		}
		UIRuntime::Get().Update();

		// Collected first, since scripts handling them may add or remove entities.
		struct Pending
		{
			Entity Target;
			UIMessage Message;
		};
		Array<Pending> pending;
		Array<UIMessage> messages;
		for (auto [entity, state] : world.View<UIStateComponent>().each())
		{
			messages.Clear();
			if (state.Document)
				state.Document->TakeMessages(messages);
			for (UIMessage& message : messages)
				pending.Add({ entity, std::move(message) });
		}
		for (const Pending& item : pending)
			Dispatch(world, item.Target, item.Message);
	}

	void UISystem::Dispatch(World& world, Entity entity, const UIMessage& message)
	{
		// The UI's own scripts first, then scene scripts, which suit menus that drive the game.
		Array<Script*> listeners;
		for (Entity target : { entity, world.GetSceneEntity() })
		{
			const ScriptComponent* scripts = world.IsValid(target) ? world.TryGet<ScriptComponent>(target) : nullptr;
			if (!scripts)
				continue;
			for (const ScriptSlot& slot : scripts->Scripts)
			{
				if (slot.Instance)
					listeners.AddUnique(slot.Instance);
			}
		}
		const Json data = Json::Parse(message.Data.View());
		for (Script* script : listeners)
			script->OnUIEvent(message.Name.View(), data);
	}

	void UISystem::OnStop(World& world)
	{
		world.GetRegistry().clear<UIStateComponent>();
	}
}
