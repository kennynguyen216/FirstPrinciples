#include "Tests/TestFramework.h"
#include "Engine/Input.h"
#include "Engine/SaveData.h"
#include "Core/Misc/Guid.h"

using namespace FP;

FP_TEST(Input_RebindsOneActionWithoutChangingOthers)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nJump Button:South\nFire Mouse:Left\nLook Stick:Right 800\n");
	FP_EXPECT(Input::RebindAction("jump", 0, "Key:G"));
	FP_EXPECT((Input::GetActionBindings("Jump") == std::vector<std::string>{ "Key:G", "Button:South" }));
	FP_EXPECT(Input::GetActionBindings("Fire")[0] == "Mouse:Left");
	FP_EXPECT(Input::GetActionBindings("Look")[0] == "Stick:Right 800");
	FP_EXPECT(!Input::RebindAction("Jump", 5, "Key:H"));
	FP_EXPECT(!Input::RebindAction("Jump", 0, "Key:Invalid"));
	FP_EXPECT(!Input::SetActionBindings("Jump", { "Key:H", "Axis:Invalid" }));
	FP_EXPECT(!Input::SetActionBindings("Jump\nFire", { "Key:H" }));
	FP_EXPECT(!Input::SetActionBindings("Jump", { "Key:H\nFire Key:J" }));
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:G");
	Input::SetKey(SDL_SCANCODE_G, true);
	FP_EXPECT(Input::IsActionDown("Jump"));
	FP_EXPECT(Input::ResetActionBindings("Jump"));
	FP_EXPECT(!Input::IsActionDown("Jump") && Input::GetActionBindings("Jump")[0] == "Key:Space");
	FP_EXPECT(Input::SetActionBindings("Fire", {}));
	FP_EXPECT(Input::GetActionBindings("Fire").empty());
	FP_EXPECT(Input::SetActionBindings("NewAction", { "Key:J" }));
	FP_EXPECT(Input::ResetActionBindings("NewAction"));
	FP_EXPECT(!Input::ResetActionBindings("NewAction"));
	Input::ResetAllActionBindings();
	FP_EXPECT(Input::GetActionBindings("Fire")[0] == "Mouse:Left");
	Input::Reset();
	Input::LoadActions(Input::GetDefaultActions());
}

FP_TEST(Input_RebindCaptureIgnoresHeldControlsAndConsumesThePress)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nJump Button:South\n");
	Input::SetKey(SDL_SCANCODE_G, true);
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::SetKey(SDL_SCANCODE_G, true);
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Listening);
	Input::SetKey(SDL_SCANCODE_H, true);
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Complete);
	FP_EXPECT(Input::GetLastReboundInput() == "Key:H");
	FP_EXPECT(Input::GetActionBindings("Jump")[1] == "Button:South");
	FP_EXPECT(!Input::IsActionDown("Jump") && !Input::WasActionPressed("Jump"));
	Input::EndFrame();
	FP_EXPECT(!Input::IsActionDown("Jump"));
	Input::SetKey(SDL_SCANCODE_H, false);
	Input::EndFrame();
	Input::SetKey(SDL_SCANCODE_H, true);
	FP_EXPECT(Input::IsActionDown("Jump") && Input::WasActionPressed("Jump"));
	Input::Reset();
	Input::LoadActions(Input::GetDefaultActions());
}

FP_TEST(Input_RebindCaptureCancelsWithoutReplacingTheBinding)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nPause Key:Escape\n");
	FP_EXPECT(!Input::BeginActionRebind("Missing", 0));
	FP_EXPECT(!Input::BeginActionRebind("Jump", 10));
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::SetKey(SDL_SCANCODE_ESCAPE, true);
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Cancelled);
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:Space");
	FP_EXPECT(!Input::WasActionPressed("Pause"));
	Input::Reset();
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::CancelActionRebind();
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Cancelled);
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::Reset();
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Cancelled);
	Input::LoadActions(Input::GetDefaultActions());
}

FP_TEST(Input_RebindCaptureSupportsMousePadsAndNeutralAxes)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nMove Key:W +Y\n");
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::SetMouseButton(MouseButton::Left, true);
	FP_EXPECT(Input::GetLastReboundInput() == "Mouse:Left");
	Input::Reset();
	FP_EXPECT(Input::BeginActionRebind("Jump", 0));
	Input::SetGamepadButton(GamepadButton::North, true);
	FP_EXPECT(Input::GetLastReboundInput() == "Button:North");
	Input::Reset();
	Input::SetGamepadAxis(GamepadAxis::LeftX, 0.9f);
	FP_EXPECT(Input::BeginActionRebind("Move", 0, true));
	Input::SetGamepadAxis(GamepadAxis::LeftX, 1);
	FP_EXPECT(Input::GetActionRebindStatus() == RebindStatus::Listening);
	Input::SetGamepadAxis(GamepadAxis::LeftX, 0);
	Input::SetGamepadAxis(GamepadAxis::LeftX, -1);
	FP_EXPECT(Input::GetLastReboundInput() == "Axis:LeftX +Y -1");
	Input::EndFrame();
	Input::SetGamepadAxis(GamepadAxis::LeftX, 0);
	Input::EndFrame();
	Input::SetGamepadAxis(GamepadAxis::LeftX, -1);
	FP_EXPECT(Input::GetActionAxis("Move").Y > 0.99f);
	Input::Reset();
	Input::LoadActions(Input::GetDefaultActions());
}

FP_TEST(Input_PlayerOverridesPersistWithoutReplacingNewProjectActions)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nFire Mouse:Left\n");
	const String slot = String("input_test_") + Guid::New().ToString();
	FP_EXPECT(Input::RebindAction("Jump", 0, "Key:G"));
	FP_EXPECT(Input::SetActionBindings("Fire", {}));
	FP_EXPECT(Input::SetActionBindings("Custom", { "Key:J" }));
	FP_EXPECT(Input::SaveActionBindings(slot.View()));
	Input::LoadActions("Jump Key:Space\nFire Mouse:Left\nInteract Key:E\n");
	FP_EXPECT(Input::LoadActionBindings(slot.View()));
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:G");
	FP_EXPECT(Input::GetActionBindings("Fire").empty());
	FP_EXPECT(Input::GetActionBindings("Custom")[0] == "Key:J");
	FP_EXPECT(Input::GetActionBindings("Interact")[0] == "Key:E");
	Input::ResetAllActionBindings();
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:Space");
	FP_EXPECT(Input::GetActionBindings("Fire")[0] == "Mouse:Left");
	FP_EXPECT(Input::SaveActionBindings(slot.View()));
	FP_EXPECT(Input::RebindAction("Jump", 0, "Key:H"));
	FP_EXPECT(Input::LoadActionBindings(slot.View()));
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:Space");
	FP_EXPECT(SaveData::Delete(slot.View()));
	Input::Reset();
	Input::LoadActions(Input::GetDefaultActions());
}

FP_TEST(Input_InvalidPlayerOverridesLeaveLiveActionsAlone)
{
	Input::Reset();
	Input::LoadActions("Jump Key:Space\nFire Mouse:Left\n");
	const String slot = String("input_test_") + Guid::New().ToString();
	SaveData settings;
	settings.SetInt("version", 1);
	settings.SetString("names", "Jump\nFire\n");
	settings.SetString("binding.Jump", "Key:H\n");
	settings.SetString("binding.Fire", "Key:Invalid\n");
	FP_EXPECT(settings.Save(slot.View()));
	FP_EXPECT(!Input::LoadActionBindings(slot.View()));
	FP_EXPECT(Input::GetActionBindings("Jump")[0] == "Key:Space");
	settings.SetInt("names", 5);
	FP_EXPECT(settings.Save(slot.View()));
	FP_EXPECT(!Input::LoadActionBindings(slot.View()));
	FP_EXPECT(Input::GetActionBindings("Fire")[0] == "Mouse:Left");
	FP_EXPECT(SaveData::Delete(slot.View()));
	FP_EXPECT(!Input::SaveActionBindings("../invalid"));
	Input::Reset();
	Input::LoadActions(Input::GetDefaultActions());
}
