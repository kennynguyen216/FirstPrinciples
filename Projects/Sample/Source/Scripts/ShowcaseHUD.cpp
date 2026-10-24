#include "Engine/Scripting/Script.h"

using namespace FP;

// Fills in the showcase HUD and acts on its buttons. Sits on the camera next to the UI component.
FPCLASS(Category = "UI")
class ShowcaseHUD : public Script
{
public:
	FPPROPERTY()
	String Character = "Noodle"; // the entity whose animation the HUD shows and drives

	FPPROPERTY()
	String Sun = "Sun"; // the light the Shadows box turns on and off

	// The HUD has buttons, so it keeps a pointer instead of the locked mouse.
	void OnStart() override { Input::SetCursorMode(CursorMode::Visible); }

	void OnUpdate(float) override
	{
		UIDocument* ui = GetUI();
		Animator* animator = GetWorld().GetAnimator(FindEntity(Character.View()));
		if (!ui || !animator)
			return;
		// Only changes go to the page, since each one runs a little JavaScript.
		const AnimState* state = animator->GetCurrentState();
		const String stateName = state ? state->Name : String("None");
		if (stateName != m_State.View())
		{
			m_State = stateName;
			ui->Set("state", stateName.View());
		}
		const float speed = animator->GetFloat("Speed");
		if (speed != m_Speed)
		{
			m_Speed = speed;
			ui->Set("speed", speed);
		}
	}

	void OnUIEvent(std::string_view name, const Json& data) override
	{
		const Entity character = FindEntity(Character.View());
		if (name == "jump")
		{
			if (Animator* animator = GetWorld().GetAnimator(character))
				animator->SetTrigger("Jump");
		}
		else if (name == "animationSpeed")
		{
			if (AnimatorComponent* animator = TryGet<AnimatorComponent>(character))
				animator->Speed = static_cast<float>(data.AsNumber(1));
		}
		else if (name == "shadows")
		{
			if (DirectionalLightComponent* light = TryGet<DirectionalLightComponent>(FindEntity(Sun.View())))
				light->CastShadows = data.AsBool(true);
		}
	}

private:
	String m_State;
	float m_Speed = -1;
};

FP_SCRIPT(ShowcaseHUD);
