#include "Engine/Input.h"

#include <cstring>

namespace FP::Input
{
	namespace
	{
		struct State
		{
			bool Down[SDL_SCANCODE_COUNT] = {};
			bool Pressed[SDL_SCANCODE_COUNT] = {};
			bool Released[SDL_SCANCODE_COUNT] = {};
			bool MouseDown[static_cast<int>(MouseButton::Count)] = {};
			bool MousePressed[static_cast<int>(MouseButton::Count)] = {};
			Vec2 MousePosition;
		};

		State GState;

		bool InRange(Key key) { return key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT; }
	}

	bool IsKeyDown(Key key) { return InRange(key) && GState.Down[key]; }
	bool WasKeyPressed(Key key) { return InRange(key) && GState.Pressed[key]; }
	bool WasKeyReleased(Key key) { return InRange(key) && GState.Released[key]; }

	bool IsMouseDown(MouseButton button) { return GState.MouseDown[static_cast<int>(button)]; }
	bool WasMousePressed(MouseButton button) { return GState.MousePressed[static_cast<int>(button)]; }
	Vec2 GetMousePosition() { return GState.MousePosition; }

	Vec2 GetMoveAxis()
	{
		Vec2 axis;
		if (IsKeyDown(SDL_SCANCODE_A) || IsKeyDown(SDL_SCANCODE_LEFT)) axis.X -= 1;
		if (IsKeyDown(SDL_SCANCODE_D) || IsKeyDown(SDL_SCANCODE_RIGHT)) axis.X += 1;
		if (IsKeyDown(SDL_SCANCODE_S) || IsKeyDown(SDL_SCANCODE_DOWN)) axis.Y -= 1;
		if (IsKeyDown(SDL_SCANCODE_W) || IsKeyDown(SDL_SCANCODE_UP)) axis.Y += 1;
		return axis;
	}

	void SetKey(Key key, bool down)
	{
		if (!InRange(key) || GState.Down[key] == down)
			return;
		GState.Down[key] = down;
		(down ? GState.Pressed : GState.Released)[key] = true;
	}

	void SetMouseButton(MouseButton button, bool down)
	{
		const int index = static_cast<int>(button);
		if (down && !GState.MouseDown[index])
			GState.MousePressed[index] = true;
		GState.MouseDown[index] = down;
	}

	void SetMousePosition(Vec2 position) { GState.MousePosition = position; }

	void ProcessEvent(const SDL_Event& event)
	{
		switch (event.type)
		{
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				if (!event.key.repeat)
					SetKey(event.key.scancode, event.type == SDL_EVENT_KEY_DOWN);
				break;
			case SDL_EVENT_MOUSE_MOTION:
				SetMousePosition({ event.motion.x, event.motion.y });
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				if (event.button.button == SDL_BUTTON_LEFT) SetMouseButton(MouseButton::Left, down);
				if (event.button.button == SDL_BUTTON_RIGHT) SetMouseButton(MouseButton::Right, down);
				if (event.button.button == SDL_BUTTON_MIDDLE) SetMouseButton(MouseButton::Middle, down);
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				Reset();
				break;
			default:
				break;
		}
	}

	void EndFrame()
	{
		std::memset(GState.Pressed, 0, sizeof(GState.Pressed));
		std::memset(GState.Released, 0, sizeof(GState.Released));
		std::memset(GState.MousePressed, 0, sizeof(GState.MousePressed));
	}

	void Reset()
	{
		const Vec2 mouse = GState.MousePosition;
		GState = State();
		GState.MousePosition = mouse;
	}
}
