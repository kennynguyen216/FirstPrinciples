#include "Engine/Input.h"
#include "Core/Misc/ModuleContext.h"

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

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
			Vec2 MouseDelta;
			CursorMode Cursor = CursorMode::Locked;
			Vec2 Scroll;
			std::string Typed;

			static constexpr int ButtonCount = static_cast<int>(GamepadButton::Count);
			bool PadDown[ButtonCount] = {};
			bool PadPressed[ButtonCount] = {};
			bool PadReleased[ButtonCount] = {};
			float PadAxis[static_cast<int>(GamepadAxis::Count)] = {};
			std::vector<SDL_Gamepad*> Pads;
		};

		constexpr float StickDeadZone = 0.2f;
		constexpr float TriggerDeadZone = 0.05f;

		// Maps what is past the dead zone back onto the full range, so movement starts smoothly.
		float PastDeadZone(float value, float zone)
		{
			const float size = std::fabs(value);
			return size <= zone ? 0.0f : std::copysign(std::min((size - zone) / (1 - zone), 1.0f), value);
		}

		// The host's state when running inside a loaded module, so scripts read real input.
		State& GetState()
		{
			static State state;
			return ResolveShared(&ModuleContext::Input, state);
		}

		bool InRange(Key key) { return key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT; }
	}

	bool IsKeyDown(Key key) { return InRange(key) && GetState().Down[key]; }
	bool WasKeyPressed(Key key) { return InRange(key) && GetState().Pressed[key]; }
	bool WasKeyReleased(Key key) { return InRange(key) && GetState().Released[key]; }

	bool IsMouseDown(MouseButton button) { return GetState().MouseDown[static_cast<int>(button)]; }
	bool WasMousePressed(MouseButton button) { return GetState().MousePressed[static_cast<int>(button)]; }
	Vec2 GetMousePosition() { return GetState().MousePosition; }
	Vec2 GetMouseDelta() { return GetState().MouseDelta; }
	void SetCursorMode(CursorMode mode) { GetState().Cursor = mode; }
	CursorMode GetCursorMode() { return GetState().Cursor; }
	Vec2 GetScroll() { return GetState().Scroll; }
	std::string_view GetTypedText() { return GetState().Typed; }

	Vec2 GetMoveAxis()
	{
		Vec2 axis;
		if (IsKeyDown(SDL_SCANCODE_A) || IsKeyDown(SDL_SCANCODE_LEFT)) axis.X -= 1;
		if (IsKeyDown(SDL_SCANCODE_D) || IsKeyDown(SDL_SCANCODE_RIGHT)) axis.X += 1;
		if (IsKeyDown(SDL_SCANCODE_S) || IsKeyDown(SDL_SCANCODE_DOWN)) axis.Y -= 1;
		if (IsKeyDown(SDL_SCANCODE_W) || IsKeyDown(SDL_SCANCODE_UP)) axis.Y += 1;
		return axis;
	}

	bool IsGamepadConnected() { return !GetState().Pads.empty(); }
	bool IsGamepadButtonDown(GamepadButton button) { return button < GamepadButton::Count && GetState().PadDown[static_cast<int>(button)]; }
	bool WasGamepadButtonPressed(GamepadButton button) { return button < GamepadButton::Count && GetState().PadPressed[static_cast<int>(button)]; }
	bool WasGamepadButtonReleased(GamepadButton button) { return button < GamepadButton::Count && GetState().PadReleased[static_cast<int>(button)]; }

	float GetGamepadAxis(GamepadAxis axis)
	{
		if (axis >= GamepadAxis::Count)
			return 0;
		const float value = GetState().PadAxis[static_cast<int>(axis)];
		const bool trigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
		return PastDeadZone(value, trigger ? TriggerDeadZone : StickDeadZone);
	}

	Vec2 GetGamepadStick(bool right)
	{
		const float* axes = GetState().PadAxis;
		Vec2 stick{ axes[right ? 2 : 0], -axes[right ? 3 : 1] };
		// The dead zone is round, so diagonals feel the same as straight pushes.
		const float length = stick.Length();
		if (length <= StickDeadZone)
			return {};
		return stick * (PastDeadZone(length, StickDeadZone) / length);
	}

	void Rumble(float low, float high, float seconds)
	{
		const auto strength = [](float value) { return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f); };
		const Uint32 ms = static_cast<Uint32>(std::max(seconds, 0.0f) * 1000.0f);
		for (SDL_Gamepad* pad : GetState().Pads)
			SDL_RumbleGamepad(pad, strength(low), strength(high), ms);
	}

	void SetGamepadButton(GamepadButton button, bool down)
	{
		if (button >= GamepadButton::Count)
			return;
		const int index = static_cast<int>(button);
		if (GetState().PadDown[index] == down)
			return;
		GetState().PadDown[index] = down;
		(down ? GetState().PadPressed : GetState().PadReleased)[index] = true;
	}

	void SetGamepadAxis(GamepadAxis axis, float value)
	{
		if (axis < GamepadAxis::Count)
			GetState().PadAxis[static_cast<int>(axis)] = std::clamp(value, -1.0f, 1.0f);
	}

	void SetKey(Key key, bool down)
	{
		if (!InRange(key) || GetState().Down[key] == down)
			return;
		GetState().Down[key] = down;
		(down ? GetState().Pressed : GetState().Released)[key] = true;
	}

	void SetMouseButton(MouseButton button, bool down)
	{
		const int index = static_cast<int>(button);
		if (down && !GetState().MouseDown[index])
			GetState().MousePressed[index] = true;
		GetState().MouseDown[index] = down;
	}

	void SetMousePosition(Vec2 position) { GetState().MousePosition = position; }
	void AddMouseDelta(Vec2 delta) { GetState().MouseDelta += delta; }
	void AddScroll(Vec2 amount) { GetState().Scroll += amount; }
	void AddTypedText(std::string_view utf8) { GetState().Typed.append(utf8); }

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
				AddMouseDelta({ event.motion.xrel, event.motion.yrel });
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
			case SDL_EVENT_MOUSE_WHEEL:
				AddScroll({ event.wheel.x, event.wheel.y });
				break;
			case SDL_EVENT_TEXT_INPUT:
				AddTypedText(event.text.text);
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				Reset();
				break;
			case SDL_EVENT_GAMEPAD_ADDED:
				if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which))
					GetState().Pads.push_back(pad);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
			{
				std::vector<SDL_Gamepad*>& pads = GetState().Pads;
				for (auto it = pads.begin(); it != pads.end(); ++it)
				{
					if (SDL_GetGamepadID(*it) == event.gdevice.which)
					{
						SDL_CloseGamepad(*it);
						pads.erase(it);
						break;
					}
				}
				// Nothing stays held on a pad that went away.
				for (int i = 0; i < State::ButtonCount; ++i)
					SetGamepadButton(static_cast<GamepadButton>(i), false);
				std::memset(GetState().PadAxis, 0, sizeof(GetState().PadAxis));
				break;
			}
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
				if (event.gbutton.button < State::ButtonCount)
					SetGamepadButton(static_cast<GamepadButton>(event.gbutton.button), event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
				break;
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				if (event.gaxis.axis < static_cast<int>(GamepadAxis::Count))
					SetGamepadAxis(static_cast<GamepadAxis>(event.gaxis.axis), static_cast<float>(event.gaxis.value) / 32767.0f);
				break;
			default:
				break;
		}
	}

	void EndFrame()
	{
		std::memset(GetState().Pressed, 0, sizeof(GetState().Pressed));
		std::memset(GetState().Released, 0, sizeof(GetState().Released));
		std::memset(GetState().MousePressed, 0, sizeof(GetState().MousePressed));
		std::memset(GetState().PadPressed, 0, sizeof(GetState().PadPressed));
		std::memset(GetState().PadReleased, 0, sizeof(GetState().PadReleased));
		GetState().Scroll = {};
		GetState().MouseDelta = {};
		GetState().Typed.clear();
	}

	void Reset()
	{
		State& state = GetState();
		const Vec2 mouse = state.MousePosition;
		const CursorMode cursor = state.Cursor;
		std::vector<SDL_Gamepad*> pads = std::move(state.Pads);
		state = State();
		state.MousePosition = mouse;
		state.Cursor = cursor;
		state.Pads = std::move(pads);
	}

	void* GetSharedState()
	{
		return &GetState();
	}
}
