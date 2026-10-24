#pragma once

#include "Engine/UI/UIRuntime.h"
#include "Engine/World.h"

namespace FP
{
	// Where the engine keeps each UI component's page while playing. Not saved.
	struct UIStateComponent
	{
		std::unique_ptr<UIDocumentInstance> Document;
		// Written by the renderer: whether the page was on screen last frame, and at what size.
		bool Drawn = false;
		uint32 Width = 0;
		uint32 Height = 0;
		float Scale = 1;
	};

	// Loads, sizes and steps the page of every UI component, and feeds input to those on screen.
	// Add it after ScriptSystem so values scripts set show the same frame.
	class UISystem : public System
	{
	public:
		const char* GetName() const override { return "UI"; }
		void Update(World& world, float deltaTime) override;

		// Drops every page so a stopped world shows none.
		void OnStop(World& world) override;

		// The page for an entity with a UI component, made on first use. Null otherwise.
		static UIDocumentInstance* For(World& world, Entity entity);

	private:
		static void Dispatch(World& world, Entity entity, const UIMessage& message);
	};
}
