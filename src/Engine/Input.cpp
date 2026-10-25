#include "Engine/Input.h"
#include "Engine/SaveData.h"
#include "Core/Misc/ModuleContext.h"
#include "Core/Log.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace FP::Input
{
	namespace
	{
		struct Binding
		{
			enum class Source : uint8
			{
				Key,
				Mouse,
				MouseDelta,
				Button,
				Axis,
				Stick,
			};
			Source From = Source::Key;
			int32 Code = 0;
			Vec2 Direction{ 1, 0 }; // where one press points, from +X, -X, +Y or -Y
			float Scale = 1;
			bool Scaled = false; // a scale was given
			bool operator==(const Binding&) const = default;
		};

		struct ActionData
		{
			std::string Name;
			std::vector<Binding> Bindings;
			bool WasDown = false; // at the end of last frame
		};

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

			std::vector<ActionData> Actions;
			std::vector<ActionData> ProjectActions;
			RebindStatus Rebind = RebindStatus::Idle;
			std::string RebindAction;
			size_t RebindIndex = 0;
			bool RebindAxes = false;
			bool AxisArmed[static_cast<int>(GamepadAxis::Count)] = {};
			bool Consuming = false;
			Binding Consumed;
			std::string LastRebound;
			bool ActionsLoaded = false; // the defaults load on first use otherwise
			float DeltaTime = 1.0f / 60.0f;
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

		bool ActionsSuppressed() { return GetState().Rebind == RebindStatus::Listening || GetState().Consuming; }
		void Capture(Binding::Source source, int32 code, bool negative = false);

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

	namespace
	{
		constexpr const char* DefaultActions = R"(# Input actions for this game. Each line binds one input to an action:
#   Action  Device:Input  [+X, -X, +Y or -Y]  [scale]
# Devices are Key (SDL key names, _ for spaces), Mouse (Left, Right, Middle or Delta),
# Button and Axis for gamepads, and Stick (Left or Right) for a whole stick.
# 2D actions read right and up as positive.

Move      Key:W            +Y
Move      Key:S            -Y
Move      Key:A            -X
Move      Key:D            +X
Move      Key:Up           +Y
Move      Key:Down         -Y
Move      Key:Left         -X
Move      Key:Right        +X
Move      Stick:Left
Move      Button:DpadUp    +Y
Move      Button:DpadDown  -Y
Move      Button:DpadLeft  -X
Move      Button:DpadRight +X

Look      Mouse:Delta
Look      Stick:Right      800    # full tilt turns like 800 pixels of mouse a second

Jump      Key:Space
Jump      Button:South
Run       Key:Left_Shift
Run       Key:Right_Shift
Run       Button:LeftStick
Fire      Mouse:Left
Fire      Axis:RightTrigger
Aim       Mouse:Right
Aim       Axis:LeftTrigger
Interact  Key:E
Interact  Button:West
Pause     Key:Escape
Pause     Button:Start
)";

		constexpr const char* ButtonNames[] = { "South", "East", "West", "North", "Back", "Guide", "Start", "LeftStick", "RightStick",
			"LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight" };
		constexpr const char* AxisNames[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };

		bool SameName(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		template <size_t N>
		int32 IndexOf(const char* const (&names)[N], std::string_view name)
		{
			for (size_t i = 0; i < N; ++i)
			{
				if (SameName(names[i], name))
					return static_cast<int32>(i);
			}
			return -1;
		}

		bool ParseBinding(std::string_view device, std::string_view input, Binding& out)
		{
			using Source = Binding::Source;
			if (SameName(device, "Key"))
			{
				std::string name(input);
				std::replace(name.begin(), name.end(), '_', ' ');
				out.From = Source::Key;
				out.Code = SDL_GetScancodeFromName(name.c_str());
				return out.Code != SDL_SCANCODE_UNKNOWN;
			}
			if (SameName(device, "Mouse"))
			{
				if (SameName(input, "Delta"))
				{
					out.From = Source::MouseDelta;
					return true;
				}
				const char* const mouse[] = { "Left", "Right", "Middle" };
				out.From = Source::Mouse;
				out.Code = IndexOf(mouse, input);
				return out.Code >= 0;
			}
			if (SameName(device, "Button"))
			{
				out.From = Source::Button;
				out.Code = IndexOf(ButtonNames, input);
				return out.Code >= 0;
			}
			if (SameName(device, "Axis"))
			{
				out.From = Source::Axis;
				out.Code = IndexOf(AxisNames, input);
				return out.Code >= 0;
			}
			if (SameName(device, "Stick"))
			{
				out.From = Source::Stick;
				out.Code = SameName(input, "Right") ? 1 : SameName(input, "Left") ? 0 : -1;
				return out.Code >= 0;
			}
			return false;
		}

		const ActionData* FindAction(std::string_view name)
		{
			if (!GetState().ActionsLoaded)
				LoadActions(DefaultActions);
			for (const ActionData& action : GetState().Actions)
			{
				if (SameName(action.Name, name))
					return &action;
			}
			return nullptr;
		}

		Vec2 Evaluate(const Binding& binding)
		{
			using Source = Binding::Source;
			switch (binding.From)
			{
				case Source::Key: return IsKeyDown(static_cast<Key>(binding.Code)) ? binding.Direction * binding.Scale : Vec2();
				case Source::Mouse: return IsMouseDown(static_cast<MouseButton>(binding.Code)) ? binding.Direction * binding.Scale : Vec2();
				case Source::Button: return IsGamepadButtonDown(static_cast<GamepadButton>(binding.Code)) ? binding.Direction * binding.Scale : Vec2();
				case Source::Axis: return binding.Direction * (GetGamepadAxis(static_cast<GamepadAxis>(binding.Code)) * binding.Scale);
				case Source::MouseDelta:
				{
					const Vec2 delta = GetMouseDelta();
					return Vec2(delta.X, -delta.Y) * binding.Scale;
				}
				case Source::Stick:
				{
					// A scale turns the stick into a speed, so it moves the same at any frame rate.
					const Vec2 stick = GetGamepadStick(binding.Code == 1);
					return binding.Scaled ? stick * (binding.Scale * GetState().DeltaTime) : stick;
				}
			}
			return {};
		}

		bool JustChanged(const Binding& binding, bool pressed)
		{
			using Source = Binding::Source;
			if (binding.From == Source::Key)
				return pressed ? WasKeyPressed(static_cast<Key>(binding.Code)) : WasKeyReleased(static_cast<Key>(binding.Code));
			if (binding.From == Source::Button)
				return pressed ? WasGamepadButtonPressed(static_cast<GamepadButton>(binding.Code)) : WasGamepadButtonReleased(static_cast<GamepadButton>(binding.Code));
			return pressed && binding.From == Source::Mouse && WasMousePressed(static_cast<MouseButton>(binding.Code));
		}

		Vec2 AxisOf(const ActionData& action)
		{
			Vec2 sum;
			for (const Binding& binding : action.Bindings)
				sum += Evaluate(binding);
			return sum;
		}

		bool DownOf(const ActionData& action)
		{
			const Vec2 axis = AxisOf(action);
			return std::max(std::fabs(axis.X), std::fabs(axis.Y)) >= 0.5f;
		}
	}

	bool IsActionDown(std::string_view name)
	{
		if (ActionsSuppressed()) return false;
		const ActionData* action = FindAction(name);
		return action && DownOf(*action);
	}

	bool WasActionPressed(std::string_view name)
	{
		if (ActionsSuppressed()) return false;
		const ActionData* action = FindAction(name);
		if (!action)
			return false;
		// A tap shorter than a frame still counts.
		for (const Binding& binding : action->Bindings)
		{
			if (JustChanged(binding, true))
				return true;
		}
		return !action->WasDown && DownOf(*action);
	}

	bool WasActionReleased(std::string_view name)
	{
		if (ActionsSuppressed()) return false;
		const ActionData* action = FindAction(name);
		if (!action)
			return false;
		for (const Binding& binding : action->Bindings)
		{
			if (JustChanged(binding, false))
				return true;
		}
		return action->WasDown && !DownOf(*action);
	}

	float GetActionValue(std::string_view name)
	{
		if (ActionsSuppressed()) return 0;
		const ActionData* action = FindAction(name);
		if (!action)
			return 0;
		float strongest = 0;
		for (const Binding& binding : action->Bindings)
		{
			const Vec2 value = Evaluate(binding);
			strongest = std::max(strongest, std::max(std::fabs(value.X), std::fabs(value.Y)));
		}
		return strongest;
	}

	Vec2 GetActionAxis(std::string_view name)
	{
		if (ActionsSuppressed()) return {};
		const ActionData* action = FindAction(name);
		return action ? AxisOf(*action) : Vec2();
	}

	static bool ParseActions(std::string_view text, std::vector<ActionData>& actions, std::vector<std::string>* errors)
	{
		bool ok = true;
		size_t start = 0;
		int32 lineNumber = 0;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string_view::npos)
				end = text.size();
			std::string_view line = text.substr(start, end - start);
			start = end + 1;
			++lineNumber;
			if (const size_t comment = line.find('#'); comment != std::string_view::npos)
				line = line.substr(0, comment);

			std::vector<std::string_view> words;
			size_t at = 0;
			while (at < line.size())
			{
				while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])))
					++at;
				size_t stop = at;
				while (stop < line.size() && !std::isspace(static_cast<unsigned char>(line[stop])))
					++stop;
				if (stop > at)
					words.push_back(line.substr(at, stop - at));
				at = stop;
			}
			if (words.empty())
				continue;

			Binding binding;
			const size_t colon = words.size() > 1 ? words[1].find(':') : std::string_view::npos;
			bool directionGiven = false;
			bool good = words.size() <= 4 && colon != std::string_view::npos && ParseBinding(words[1].substr(0, colon), words[1].substr(colon + 1), binding);
			for (size_t i = 2; good && i < words.size(); ++i)
			{
				const std::string_view word = words[i];
				if (word == "+X" || word == "-X" || word == "+Y" || word == "-Y")
				{
					if (directionGiven) { good = false; break; }
					directionGiven = true;
				}
				if (word == "+X") binding.Direction = { 1, 0 };
				else if (word == "-X") binding.Direction = { -1, 0 };
				else if (word == "+Y") binding.Direction = { 0, 1 };
				else if (word == "-Y") binding.Direction = { 0, -1 };
				else
				{
					if (binding.Scaled) { good = false; break; }
					char* stop = nullptr;
					const std::string number(word);
					binding.Scale = std::strtof(number.c_str(), &stop);
					binding.Scaled = true;
					good = stop && *stop == 0 && std::isfinite(binding.Scale);
				}
			}
			if (!good)
			{
				ok = false;
				if (errors)
					errors->push_back("Line " + std::to_string(lineNumber) + ": " + std::string(line));
				continue;
			}

			auto found = std::find_if(actions.begin(), actions.end(), [&](const ActionData& a) { return SameName(a.Name, words[0]); });
			if (found == actions.end())
			{
				actions.push_back({ std::string(words[0]), {}, false });
				found = actions.end() - 1;
			}
			found->Bindings.push_back(binding);
		}
		return ok;
	}

	bool ValidateActions(std::string_view text, std::vector<std::string>* errors)
	{
		std::vector<ActionData> actions;
		return ParseActions(text, actions, errors);
	}

	bool LoadActions(std::string_view text, std::vector<std::string>* errors)
	{
		std::vector<ActionData> actions;
		const bool ok = ParseActions(text, actions, errors);
		GetState().ProjectActions = actions;
		GetState().Actions = std::move(actions);
		GetState().ActionsLoaded = true;
		GetState().Rebind = RebindStatus::Idle;
		GetState().Consuming = false;
		return ok;
	}

	std::vector<std::string> GetBindingInputs(std::string_view device)
	{
		std::vector<std::string> names;
		if (SameName(device, "Key"))
		{
			for (int code = 1; code < SDL_SCANCODE_COUNT; ++code)
			{
				std::string name = SDL_GetScancodeName(static_cast<Key>(code));
				if (name.empty() || name.find('#') != std::string::npos) continue;
				std::replace(name.begin(), name.end(), ' ', '_');
				if (SDL_GetScancodeFromName(SDL_GetScancodeName(static_cast<Key>(code))) == code)
					names.push_back(std::move(name));
			}
			std::sort(names.begin(), names.end());
		}
		else if (SameName(device, "Mouse")) names = { "Left", "Right", "Middle", "Delta" };
		else if (SameName(device, "Button")) names.assign(std::begin(ButtonNames), std::end(ButtonNames));
		else if (SameName(device, "Axis")) names.assign(std::begin(AxisNames), std::end(AxisNames));
		else if (SameName(device, "Stick")) names = { "Left", "Right" };
		return names;
	}

	bool LoadActionsFile(const char* path)
	{
		std::string text;
		if (SDL_IOStream* file = SDL_IOFromFile(path, "rb"))
		{
			const Sint64 size = SDL_GetIOSize(file);
			text.resize(size > 0 ? static_cast<size_t>(size) : 0);
			if (!text.empty())
				SDL_ReadIO(file, text.data(), text.size());
			SDL_CloseIO(file);
		}
		else
		{
			LoadActions(DefaultActions);
			return true;
		}
		std::vector<std::string> errors;
		const bool ok = LoadActions(text, &errors);
		for (const std::string& error : errors)
			FP_LOG_WARN("%s could not be read. %s", path, error.c_str());
		return ok;
	}

	std::string_view GetDefaultActions() { return DefaultActions; }

	std::vector<std::string> GetActionNames()
	{
		FindAction({});
		std::vector<std::string> names;
		for (const ActionData& action : GetState().Actions)
			names.push_back(action.Name);
		return names;
	}

	namespace
	{
		ActionData* FindIn(std::vector<ActionData>& actions, std::string_view name)
		{
			for (ActionData& action : actions)
				if (SameName(action.Name, name)) return &action;
			return nullptr;
		}

		bool ValidActionName(std::string_view name)
		{
			if (name.empty()) return false;
			for (unsigned char c : name)
				if (std::isspace(c) || c == '#' || c == 0) return false;
			return true;
		}

		std::string BindingText(const Binding& binding)
		{
			using Source = Binding::Source;
			std::string text;
			if (binding.From == Source::Key)
			{
				text = "Key:" + std::string(SDL_GetScancodeName(static_cast<Key>(binding.Code)));
				std::replace(text.begin(), text.end(), ' ', '_');
			}
			else if (binding.From == Source::Mouse) text = std::string("Mouse:") + (binding.Code == 0 ? "Left" : binding.Code == 1 ? "Right" : "Middle");
			else if (binding.From == Source::MouseDelta) text = "Mouse:Delta";
			else if (binding.From == Source::Button) text = std::string("Button:") + ButtonNames[binding.Code];
			else if (binding.From == Source::Axis) text = std::string("Axis:") + AxisNames[binding.Code];
			else text = binding.Code ? "Stick:Right" : "Stick:Left";
			if (binding.Direction.X < 0) text += " -X";
			else if (binding.Direction.Y > 0) text += " +Y";
			else if (binding.Direction.Y < 0) text += " -Y";
			if (binding.Scaled)
			{
				char scale[32];
				std::snprintf(scale, sizeof(scale), " %.9g", binding.Scale);
				text += scale;
			}
			return text;
		}

		bool ReadBindings(std::string_view name, const std::vector<std::string>& bindings, ActionData& out)
		{
			if (!ValidActionName(name)) return false;
			std::string text;
			for (const std::string& binding : bindings)
			{
				if (binding.find_first_of("\r\n") != std::string::npos || binding.find('\0') != std::string::npos) return false;
				text += std::string(name) + " " + binding + "\n";
			}
			std::vector<ActionData> parsed;
			if (!ParseActions(text, parsed, nullptr)) return false;
			if (!bindings.empty() && (parsed.size() != 1 || parsed[0].Bindings.size() != bindings.size())) return false;
			out = bindings.empty() ? ActionData{ std::string(name), {}, false } : std::move(parsed[0]);
			return true;
		}

		void Capture(Binding::Source source, int32 code, bool negative)
		{
			State& state = GetState();
			if (state.Rebind != RebindStatus::Listening) return;
			if (source == Binding::Source::Key)
			{
				const std::string name = SDL_GetScancodeName(static_cast<Key>(code));
				if (name.empty() || name.find('#') != std::string::npos) return;
			}
			state.Consuming = true;
			state.Consumed = Binding();
			state.Consumed.From = source;
			state.Consumed.Code = code;
			if (source == Binding::Source::Key && code == SDL_SCANCODE_ESCAPE)
			{
				state.Rebind = RebindStatus::Cancelled;
				return;
			}
			ActionData* action = FindIn(state.Actions, state.RebindAction);
			if (!action || state.RebindIndex >= action->Bindings.size())
			{
				state.Rebind = RebindStatus::Cancelled;
				return;
			}
			Binding& binding = action->Bindings[state.RebindIndex];
			binding.From = source;
			binding.Code = code;
			if (negative)
			{
				binding.Scale *= -1;
				binding.Scaled = true;
			}
			state.LastRebound = BindingText(binding);
			state.Rebind = RebindStatus::Complete;
			action->WasDown = false;
		}
	}

	std::vector<std::string> GetActionBindings(std::string_view name)
	{
		std::vector<std::string> bindings;
		if (const ActionData* action = FindAction(name))
			for (const Binding& binding : action->Bindings) bindings.push_back(BindingText(binding));
		return bindings;
	}

	bool SetActionBindings(std::string_view name, const std::vector<std::string>& bindings)
	{
		ActionData replacement;
		if (!ReadBindings(name, bindings, replacement)) return false;
		FindAction({});
		if (ActionData* action = FindIn(GetState().Actions, name))
		{
			replacement.Name = action->Name;
			*action = std::move(replacement);
		}
		else GetState().Actions.push_back(std::move(replacement));
		if (SameName(GetState().RebindAction, name)) CancelActionRebind();
		return true;
	}

	bool RebindAction(std::string_view name, size_t index, std::string_view binding)
	{
		std::vector<std::string> bindings = GetActionBindings(name);
		if (index >= bindings.size()) return false;
		bindings[index] = binding;
		return SetActionBindings(name, bindings);
	}

	bool ResetActionBindings(std::string_view name)
	{
		FindAction({});
		State& state = GetState();
		ActionData* action = FindIn(state.Actions, name);
		if (!action) return false;
		if (const ActionData* original = FindIn(state.ProjectActions, name))
		{
			*action = *original;
			action->WasDown = false;
		}
		else state.Actions.erase(std::remove_if(state.Actions.begin(), state.Actions.end(), [&](const ActionData& item) { return SameName(item.Name, name); }), state.Actions.end());
		if (SameName(state.RebindAction, name)) CancelActionRebind();
		return true;
	}

	void ResetAllActionBindings()
	{
		FindAction({});
		GetState().Actions = GetState().ProjectActions;
		CancelActionRebind();
	}

	bool SaveActionBindings(std::string_view slot)
	{
		FindAction({});
		SaveData settings;
		settings.SetInt("version", 1);
		std::string names;
		for (const ActionData& action : GetState().Actions)
		{
			const ActionData* original = FindIn(GetState().ProjectActions, action.Name);
			if (original && original->Bindings == action.Bindings) continue;
			names += action.Name + "\n";
			std::string bindings;
			for (const Binding& binding : action.Bindings) bindings += BindingText(binding) + "\n";
			settings.SetString("binding." + action.Name, bindings);
		}
		settings.SetString("names", names);
		return settings.Save(slot);
	}

	bool LoadActionBindings(std::string_view slot)
	{
		SaveData settings;
		if (!settings.Load(slot) || settings.GetInt("version") != 1 || !settings.Has("names")) return false;
		const String names = settings.GetString("names", "\n");
		std::istringstream list(std::string(names.View()));
		std::vector<ActionData> overrides;
		std::string name;
		while (std::getline(list, name))
		{
			if (!ValidActionName(name) || FindIn(overrides, name) || !settings.Has("binding." + name)) return false;
			const String text = settings.GetString("binding." + name, "\n");
			std::istringstream lines(std::string(text.View()));
			std::vector<std::string> bindings;
			std::string binding;
			while (std::getline(lines, binding)) bindings.push_back(binding);
			ActionData action;
			if (!ReadBindings(name, bindings, action)) return false;
			overrides.push_back(std::move(action));
		}
		FindAction({});
		std::vector<ActionData> actions = GetState().ProjectActions;
		for (ActionData& replacement : overrides)
		{
			if (ActionData* action = FindIn(actions, replacement.Name)) *action = std::move(replacement);
			else actions.push_back(std::move(replacement));
		}
		GetState().Actions = std::move(actions);
		CancelActionRebind();
		return true;
	}

	bool BeginActionRebind(std::string_view name, size_t index, bool allowAxes)
	{
		const ActionData* action = FindAction(name);
		if (!action || index >= action->Bindings.size()) return false;
		State& state = GetState();
		state.RebindAction = action->Name;
		state.RebindIndex = index;
		state.RebindAxes = allowAxes;
		state.Rebind = RebindStatus::Listening;
		state.LastRebound.clear();
		state.Consuming = false;
		for (int32 i = 0; i < static_cast<int32>(GamepadAxis::Count); ++i)
			state.AxisArmed[i] = std::fabs(state.PadAxis[i]) < 0.25f;
		return true;
	}

	void CancelActionRebind()
	{
		if (GetState().Rebind == RebindStatus::Listening) GetState().Rebind = RebindStatus::Cancelled;
	}

	RebindStatus GetActionRebindStatus() { return GetState().Rebind; }
	std::string GetLastReboundInput() { return GetState().LastRebound; }

	void SetDeltaTime(float seconds) { GetState().DeltaTime = seconds; }

	void SetGamepadButton(GamepadButton button, bool down)
	{
		if (button >= GamepadButton::Count)
			return;
		const int index = static_cast<int>(button);
		if (GetState().PadDown[index] == down)
			return;
		GetState().PadDown[index] = down;
		(down ? GetState().PadPressed : GetState().PadReleased)[index] = true;
		if (down) Capture(Binding::Source::Button, index);
	}

	void SetGamepadAxis(GamepadAxis axis, float value)
	{
		if (axis >= GamepadAxis::Count || !std::isfinite(value)) return;
		State& state = GetState();
		const int index = static_cast<int>(axis);
		state.PadAxis[index] = std::clamp(value, -1.0f, 1.0f);
		if (std::fabs(value) < 0.25f) state.AxisArmed[index] = true;
		if (state.RebindAxes && state.AxisArmed[index] && std::fabs(value) > 0.75f)
			Capture(Binding::Source::Axis, index, value < 0);
	}

	void SetKey(Key key, bool down)
	{
		if (!InRange(key) || GetState().Down[key] == down)
			return;
		GetState().Down[key] = down;
		(down ? GetState().Pressed : GetState().Released)[key] = true;
		if (down) Capture(Binding::Source::Key, key);
	}

	void SetMouseButton(MouseButton button, bool down)
	{
		const int index = static_cast<int>(button);
		if (down && !GetState().MouseDown[index])
		{
			GetState().MousePressed[index] = true;
			Capture(Binding::Source::Mouse, index);
		}
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
		for (ActionData& action : GetState().Actions)
			action.WasDown = !ActionsSuppressed() && DownOf(action);
		std::memset(GetState().Pressed, 0, sizeof(GetState().Pressed));
		std::memset(GetState().Released, 0, sizeof(GetState().Released));
		std::memset(GetState().MousePressed, 0, sizeof(GetState().MousePressed));
		std::memset(GetState().PadPressed, 0, sizeof(GetState().PadPressed));
		std::memset(GetState().PadReleased, 0, sizeof(GetState().PadReleased));
		GetState().Scroll = {};
		GetState().MouseDelta = {};
		GetState().Typed.clear();
		State& state = GetState();
		if (state.Consuming && Evaluate(state.Consumed).LengthSquared() < 0.0625f)
			state.Consuming = false;
	}

	void Reset()
	{
		State& state = GetState();
		const Vec2 mouse = state.MousePosition;
		const CursorMode cursor = state.Cursor;
		std::vector<SDL_Gamepad*> pads = std::move(state.Pads);
		std::vector<ActionData> actions = std::move(state.Actions);
		std::vector<ActionData> projectActions = std::move(state.ProjectActions);
		const RebindStatus rebind = state.Rebind == RebindStatus::Listening ? RebindStatus::Cancelled : RebindStatus::Idle;
		const float deltaTime = state.DeltaTime;
		const bool loaded = state.ActionsLoaded;
		state = State();
		state.Actions = std::move(actions);
		state.ProjectActions = std::move(projectActions);
		state.Rebind = rebind;
		state.ActionsLoaded = loaded;
		state.DeltaTime = deltaTime;
		state.MousePosition = mouse;
		state.Cursor = cursor;
		state.Pads = std::move(pads);
		for (ActionData& action : state.Actions)
			action.WasDown = false;
	}

	void* GetSharedState()
	{
		return &GetState();
	}
}
