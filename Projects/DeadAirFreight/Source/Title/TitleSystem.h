#pragma once

#include "Title/LanDiscovery.h"
#include "Title/TitleComponents.h"

// The title screen: Host Game or Join Game, then a lobby until the host starts the match.
//
// Hosting or joining starts a network session from here, so the game no longer needs --host or
// --join (starting with them skips straight to the lobby). Hosting alone plays solo. Joiners see the
// games hosts announce on the LAN, and can type an address instead. Back in the lobby leaves the game
// for the title screen again.
//
// Past the first screen the page goes see-through over the truck, idling in space, seen from its
// right side. Starting fades to black, holds it while the match loads, then fades into the game.
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
	// The truck bobbing in space behind the join list and lobby. Off puts it and the camera back.
	void ShowTruck(FP::World& world, bool on, float deltaTime);
	// Black while the match loads, then fading into the game with the countdown on top, then the
	// page goes after GO.
	void Reveal(FP::World& world, float deltaTime);
	// The Driver's cab starts at GO. Until then the Driver stands in it anyway and can look around,
	// rather than staring at a still picture through the countdown.
	void PreviewCab(FP::World& world);

	FP::Entity m_Screen = FP::NullEntity;
	LanDiscovery m_Discovery;
	FP::uint16 m_HostedPort = 0;
	std::string m_Status;
	bool m_Joining = false; // on the join list rather than the first screen
	float m_SincePage = 0;

	FP::Entity m_Truck = FP::NullEntity;
	FP::Entity m_Camera = FP::NullEntity;
	FP::Entity m_Depot = FP::NullEntity;
	FP::MeshComponent m_DepotMesh; // while it is hidden
	FP::TransformComponent m_TruckRest;
	FP::TransformComponent m_CameraRest;
	bool m_ShowingTruck = false;
	float m_BobTime = 0;

	enum class Fade { None, Black, Clearing, Done };
	Fade m_Fade = Fade::None;
	float m_FadeTime = 0;
	int m_SteadyFrames = 0;
	std::string m_CountdownLabel;
	float m_GoTime = 0;

	// From the Driver's settings, for the countdown's look around the cab.
	float m_EyeHeight = 2.4f;
	float m_LookSensitivity = 0.15f;
};
