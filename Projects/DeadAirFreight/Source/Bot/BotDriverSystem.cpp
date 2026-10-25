#include "Bot/BotDriverSystem.h"
#include "Driver/Cab.h"
#include "Driver/DriverSettings.h"
#include "Driver/Truck.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"
#include "Title/TitleComponents.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// Once a rival is in, the lobby starts this soon.
	constexpr float StartAfterJoin = 5;
	// How briskly the bot turns its view toward where it wants to go, and the most mouse it moves in
	// a frame, so it steers like a steady hand rather than snapping.
	constexpr float AimGain = 3.5f;
	constexpr float MostPixelsPerFrame = 40;
	// A little wander in its aim, so it is not a perfect line.
	constexpr float WanderDegrees = 2.5f;
}

bool BotDriverSystem::IsEnabled()
{
	const char* flag = std::getenv("DAF_BOT");
	return flag && *flag && *flag != '0';
}

void BotDriverSystem::OnStart(World&)
{
	m_Time = 0;
	m_SawRival = false;
	DriverSettings settings;
	m_Sensitivity = LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), settings) && settings.MouseSensitivity > 0 ? settings.MouseSensitivity : 0.15f;
	FP_LOG("Bot: this copy plays itself");
}

void BotDriverSystem::Update(World& world, float deltaTime)
{
	m_Time += deltaTime;
	MatchStateComponent* state = GetState(world);
	if (!state)
		return;

	// On the title screen it presses Host, like a player, so the game is announced to the network.
	if (state->Phase == MatchPhase::Title)
	{
		// The page's script makes the request on a player's first click, so the bot makes its own.
		for (auto [entity, title] : world.View<TitleScreenComponent>().each())
		{
			if (m_Time < 1)
				break;
			TitleRequestComponent& request = world.Has<TitleRequestComponent>(entity) ? world.Get<TitleRequestComponent>(entity) : world.Add<TitleRequestComponent>(entity);
			if (request.Action.IsEmpty() && !m_Hosted)
			{
				request.Action = "host";
				m_Hosted = true;
				FP_LOG("Bot: hosting");
			}
		}
		return;
	}

	// The lobby waits for a rival, then counts down briefly.
	if (state->Phase == MatchPhase::Lobby && GameNet::IsHost(world))
	{
		int32 seated = 0;
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
			seated += member.Slot >= 0 ? 1 : 0;
		if (seated >= 2 && !m_SawRival)
		{
			m_SawRival = true;
			FP_LOG("Bot: a rival joined, starting in %.0f s", StartAfterJoin);
		}
		state->Countdown = m_SawRival ? std::min(state->Countdown, StartAfterJoin) : 99;
		return;
	}
	if (state->Phase != MatchPhase::Racing)
		return;

	const Entity truckEntity = world.FindEntityByName("Truck");
	const Entity body = world.FindEntityByName("Driver");
	const Entity colony = world.FindEntityByName("Colony");
	if (!world.IsValid(truckEntity) || !world.IsValid(body) || !world.IsValid(colony))
		return;
	TruckState* truck = world.TryGet<TruckState>(truckEntity);
	DriverState* driver = world.TryGet<DriverState>(body);
	if (!truck || !driver)
		return; // the Driver's systems have not started yet

	// Straight to the wheel, the way a player walks up and presses E.
	if (!driver->Seated)
	{
		driver->InCab = true;
		driver->Seated = true;
		FP_LOG("Bot: at the wheel");
	}

	// Turn the view toward the colony, with a slow wander. The Driver's systems swing the nose to
	// follow the view, exactly as for a player's mouse.
	const Vec3 target = world.Get<TransformComponent>(colony).Position;
	const Vec3 toward = target - truck->Position;
	const float flat = std::sqrt(toward.X * toward.X + toward.Z * toward.Z);
	const float wantYaw = YawToward(truck->Position, target) + WanderDegrees * std::sin(m_Time * 0.37f);
	const float wantPitch = std::atan2(toward.Y, std::max(flat, 1.0f)) * RadToDeg + WanderDegrees * 0.5f * std::sin(m_Time * 0.53f + 1);
	const Vec2 error{ std::remainder(wantYaw - truck->Look.X, 360.0f), wantPitch - truck->Look.Y };

	// The look moves by minus the mouse times the sensitivity.
	Vec2 mouse = error * (-std::min(1.0f, AimGain * deltaTime) / m_Sensitivity);
	mouse.X = std::clamp(mouse.X, -MostPixelsPerFrame, MostPixelsPerFrame);
	mouse.Y = std::clamp(mouse.Y, -MostPixelsPerFrame, MostPixelsPerFrame);
	Input::AddMouseDelta(mouse);
}
