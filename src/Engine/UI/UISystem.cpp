#include "Engine/UI/UISystem.h"
#include "Engine/Json.h"
#include "Engine/Scripting/Script.h"

namespace FP
{
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

	void UISystem::Update(World& world, float)
	{
		for (auto [entity, ui] : world.View<UIComponent>().each())
		{
			UIDocumentInstance* document = For(world, entity);
			UIStateComponent& state = world.Get<UIStateComponent>(entity);
			if (document->GetPage() != ui.Page.View())
				document->LoadPage(ui.Page.View());
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
