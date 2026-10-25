#pragma once

#include "Engine/Engine.h"

// A stand-in rival for testing against: with the environment variable DAF_BOT set, this copy of the
// game plays itself. It hosts, holds the lobby until someone joins, then sits at the wheel and flies
// for the colony by feeding the engine the mouse movement a player would make, so the truck is
// driven by the same code as anyone's. Started on its own, everything else plays normally.
class BotDriverSystem : public FP::System
{
public:
	const char* GetName() const override { return "Bot Driver"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

	static bool IsEnabled();

private:
	float m_Time = 0;
	bool m_SawRival = false;
	bool m_Hosted = false; // once per run: back on the title screen after a race, it stays there
	float m_Sensitivity = 0.15f; // the Driver's mouse sensitivity
};
