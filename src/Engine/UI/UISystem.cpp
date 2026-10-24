#include "Engine/UI/UISystem.h"

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
	}

	void UISystem::OnStop(World& world)
	{
		world.GetRegistry().clear<UIStateComponent>();
	}
}
