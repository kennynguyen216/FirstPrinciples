#pragma once

#include "Title/LanDiscovery.h"
#include "Title/TitleComponents.h"

// The title screen: Host Game or Join Game, then a lobby until the host starts the match.
//
// Hosting or joining starts a network session from here, so the game no longer needs --host or
// --join (starting with them skips straight to the lobby). Hosting alone plays solo. Joiners see the
// games hosts announce on the LAN, and can type an address instead. Back in the lobby leaves the game
// for the title screen again. The page hides at the countdown.
class TitleSystem : public FP::System
{
public:
	const char* GetName() const override { return "Title"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
	void OnStop(FP::World& world) override;

private:
	void Host(FP::World& world, const TitleScreenComponent& title);
	void Join(FP::World& world, const FP::String& address);
	// Back from the lobby to the title screen: leaves the game and forgets everyone else in it.
	void Leave(FP::World& world);
	void ShowPage(FP::World& world);

	FP::Entity m_Screen = FP::NullEntity;
	LanDiscovery m_Discovery;
	FP::uint16 m_HostedPort = 0;
	std::string m_Status;
	bool m_Joining = false; // on the join list rather than the first screen
	float m_SincePage = 0;
};
