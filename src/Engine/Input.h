#pragma once

#include "Engine/Math.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

namespace FP
{
	// Keys are SDL scancodes so they mean the same physical key on every keyboard layout.
	using Key = SDL_Scancode;

	enum class MouseButton : uint8
	{
		Left,
		Right,
		Middle,
		Count,
	};

	// Global input state for the current frame. The runtime feeds it SDL events and the editor
	// feeds it Qt events, so game code reads input the same way in both.
	namespace Input
	{
		bool IsKeyDown(Key key);
		bool WasKeyPressed(Key key);
		bool WasKeyReleased(Key key);

		bool IsMouseDown(MouseButton button);
		bool WasMousePressed(MouseButton button);

		// Pixels from the top left of the game view.
		Vec2 GetMousePosition();

		// -1 to 1 on each axis from WASD and the arrow keys.
		Vec2 GetMoveAxis();

		// ---- Fed by the platform layer -------------------------------------

		void SetKey(Key key, bool down);
		void SetMouseButton(MouseButton button, bool down);
		void SetMousePosition(Vec2 position);
		void ProcessEvent(const SDL_Event& event);

		// Call once after the frame is updated to clear pressed and released flags.
		void EndFrame();

		// Releases everything, for example when the window loses focus.
		void Reset();
	}
}
