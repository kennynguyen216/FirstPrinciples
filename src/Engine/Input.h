#pragma once

#include "Engine/Math.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

#include <string>
#include <string_view>
#include <vector>

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

	// Named by position so they mean the same on every pad. South is A on Xbox and Cross on
	// PlayStation. Same order as SDL.
	enum class GamepadButton : uint8
	{
		South,
		East,
		West,
		North,
		Back,
		Guide,
		Start,
		LeftStick,
		RightStick,
		LeftShoulder,
		RightShoulder,
		DpadUp,
		DpadDown,
		DpadLeft,
		DpadRight,
		Count,
	};

	enum class GamepadAxis : uint8
	{
		LeftX,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		Count,
	};

	// How the game wants the mouse while it has focus.
	enum class CursorMode : uint8
	{
		Visible, // a normal pointer
		Hidden,  // moves as usual but is not drawn, for games that draw their own
		Locked,  // hidden and held in place, for mouse look. Read GetMouseDelta for movement
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

		// How far the mouse moved this frame in pixels, also while locked. Positive Y is down.
		Vec2 GetMouseDelta();

		// Locked by default when play starts. The runtime and the editor viewport apply it while
		// they have focus. In the editor Escape frees the mouse until the viewport is clicked.
		void SetCursorMode(CursorMode mode);
		CursorMode GetCursorMode();

		// Wheel movement this frame. Positive Y scrolls up, positive X scrolls right.
		Vec2 GetScroll();

		// Text typed this frame as UTF-8, for text boxes. Empty on most frames.
		std::string_view GetTypedText();

		// -1 to 1 on each axis from WASD and the arrow keys.
		Vec2 GetMoveAxis();

		// ---- Gamepads ------------------------------------------------------
		// Every connected pad counts, so whichever one is picked up plays.

		bool IsGamepadConnected();
		bool IsGamepadButtonDown(GamepadButton button);
		bool WasGamepadButtonPressed(GamepadButton button);
		bool WasGamepadButtonReleased(GamepadButton button);

		// Sticks go from -1 to 1 with right and down positive, triggers from 0 to 1. Small
		// movements near the middle read as zero, since sticks rarely rest exactly centered.
		float GetGamepadAxis(GamepadAxis axis);

		// A whole stick with right and up positive, at most 1 long.
		Vec2 GetGamepadStick(bool right);

		// Shakes every pad. Strengths go from 0 to 1, low is the heavy motor.
		void Rumble(float low, float high, float seconds);

		// ---- Actions -------------------------------------------------------
		// Named inputs like Jump or Move that keys, mouse buttons and gamepads all drive, so
		// games ask what the player wants instead of which key is down. The bindings come from
		// Content/Input.actions, or built in defaults when a project has none.

		bool IsActionDown(std::string_view action);
		bool WasActionPressed(std::string_view action);
		bool WasActionReleased(std::string_view action);

		// The strongest of the action's bindings, 0 to 1 for buttons and triggers.
		float GetActionValue(std::string_view action);

		// For actions like Move and Look, with right and up positive. Keys and buttons add 1
		// along the direction they are bound to, sticks add their position.
		Vec2 GetActionAxis(std::string_view action);

		// Replaces every action with the bindings in text, one per line:
		//   Action  Device:Input  [+X, -X, +Y or -Y]  [scale]
		// Returns false, with each bad line in errors, when some lines could not be read. The
		// rest still load.
		bool LoadActions(std::string_view text, std::vector<std::string>* errors = nullptr);

		// Loads the file, or the defaults when it does not exist.
		bool LoadActionsFile(const char* path);

		// The bindings every new project starts with.
		std::string_view GetDefaultActions();

		// Every action name, in the order they were first bound.
		std::vector<std::string> GetActionNames();

		// ---- Fed by the platform layer -------------------------------------

		void SetKey(Key key, bool down);
		void SetMouseButton(MouseButton button, bool down);
		void SetMousePosition(Vec2 position);
		void AddMouseDelta(Vec2 delta);
		void AddScroll(Vec2 amount);
		void AddTypedText(std::string_view utf8);
		void SetGamepadButton(GamepadButton button, bool down);
		void SetGamepadAxis(GamepadAxis axis, float value);
		void ProcessEvent(const SDL_Event& event);

		// The frame's length, so held sticks can turn at a steady speed.
		void SetDeltaTime(float seconds);

		// Call once after the frame is updated to clear pressed and released flags.
		void EndFrame();

		// Releases everything, for example when the window loses focus. Keeps the cursor mode
		// and connected pads.
		void Reset();

		// For ModuleContext so loaded modules read the host's input.
		void* GetSharedState();
	}
}
