#pragma once

#include "Engine/Engine.h"
#include "Engine/Net/NetSession.h"

// Keeps a networked match in step with the host (see GameNet.h).
//
// The host sends the match state a few times a second and each crew's bay (tanks, creatures and
// airlock) twenty times a second, and applies the hits, repairs and vents other players send it.
// Everyone else applies what the host sends. The truck goes through the driver bridge, which runs
// right next to the Driver's systems.
class NetSyncSystem : public FP::System
{
public:
	const char* GetName() const override { return "NetSync"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;

private:
	void Handle(FP::World& world, const FP::NetEvent& event);
	void SendMatchState(FP::World& world);
	void SendBayState(FP::World& world, FP::int32 crew);

	float m_SinceMatchState = 0;
	float m_SinceBayState = 0;
};
