#include "Title/TitleSystem.h"
#include "Bridge/DriverBridge.h"
#include "Driver/DriverSettings.h"
#include "Driver/WeatherFetch.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Net/NetworkSystem.h"
#include "Engine/UI/UISystem.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// How often the page hears the lobby and the games found.
	constexpr float PageInterval = 0.25f;

	// The truck behind the join list and lobby: seen from off its right side, a little above, bobbing
	// gently on the spot like it is idling.
	constexpr const char* TruckName = "Truck";
	constexpr const char* CameraName = "DriverCamera";
	// The launch pad block beside the truck fills the shot, so it steps out while the truck is shown.
	constexpr const char* DepotName = "Depot";
	const Vec3 TruckMiddle{ 0, 0.25f, 2.95f }; // the middle of the hull, in the model's own units
	const Vec3 ShowcaseOffset{ 27, 6, 3 }; // camera from the middle of the hull
	const Vec3 ShowcaseAim{ 0, 2.5f, 0 }; // looks a little above the hull, so the truck sits under the menu
	constexpr float BobHeight = 0.35f;
	constexpr float BobRate = 1.2f;
	constexpr float BobPitch = 1.2f; // degrees
	constexpr float BobRoll = 1.6f;

	// The fade into the game. Black holds at least this long, and until this many frames in a row
	// come quickly, so the hitch of the match loading happens out of sight. Then it clears over the
	// last time (the page's own CSS transition matches it).
	constexpr float BlackAtLeast = 0.3f;
	constexpr int SteadyFramesNeeded = 8;
	constexpr float SteadyFrameSeconds = 0.1f;
	constexpr float ClearSeconds = 0.8f;
	// GO stays up this long after the countdown.
	constexpr float GoSeconds = 0.8f;
	// Where the Driver stands in the cab at GO.
	constexpr const char* CabBodyName = "Driver";

	NetworkSystem* FindNetwork(World& world)
	{
		for (const RefPtr<System>& system : world.GetSystems())
		{
			if (NetworkSystem* network = dynamic_cast<NetworkSystem*>(system.Get()))
				return network;
		}
		return nullptr;
	}

	int32 CountPlayers(World& world)
	{
		int32 players = 0;
		for (auto [entity, member] : world.View<CrewMemberComponent>().each())
			players += member.Slot >= 0 ? 1 : 0;
		return players;
	}

	// Escapes text for a JSON string.
	std::string Quote(const std::string& text)
	{
		std::string out = "\"";
		for (const char c : text)
		{
			if (c == '"' || c == '\\')
				out += '\\';
			if (static_cast<unsigned char>(c) >= 0x20)
				out += c;
		}
		return out + "\"";
	}
}

void TitleSystem::OnStart(World& world)
{
	m_Screen = NullEntity;
	for (auto [entity, title] : world.View<TitleScreenComponent>().each())
		m_Screen = entity;
	m_HostedPort = 0;
	m_Joining = false;
	m_Status.clear();
	m_SincePage = PageInterval;
	// The match fetches live space weather and asteroids as it starts, which froze the game for
	// seconds. Started now, it downloads while the players are on these screens.
	DriverSettings settings;
	if (LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), settings))
	{
		PrefetchMatchSources(settings);
		m_EyeHeight = settings.EyeHeight;
		m_LookSensitivity = settings.LookSensitivity;
	}
	m_CountdownLabel.clear();
	m_GoTime = 0;
	m_ShowingTruck = false;
	m_BobTime = 0;
	m_Fade = Fade::None;
	m_Truck = world.FindEntityByName(TruckName);
	m_Camera = world.FindEntityByName(CameraName);
	m_Depot = world.FindEntityByName(DepotName);
	if (world.IsValid(m_Truck))
		m_TruckRest = world.Get<TransformComponent>(m_Truck);
	if (world.IsValid(m_Camera))
		m_CameraRest = world.Get<TransformComponent>(m_Camera);

	MatchStateComponent* state = GetState(world);
	if (!state)
		return;
	// Started with --host or --join: the session is already up, so go straight to the lobby.
	if (GameNet::IsNetworked(world) || m_Screen == NullEntity)
	{
		if (m_Screen != NullEntity)
			world.Get<UIComponent>(m_Screen).Visible = false;
		return;
	}
	state->Phase = MatchPhase::Title;
	Input::SetCursorMode(CursorMode::Visible);
	const TitleScreenComponent& title = world.Get<TitleScreenComponent>(m_Screen);
	if (!m_Discovery.Listen(static_cast<uint16>(title.DiscoveryPort)))
		FP_LOG_WARN("Title: could not listen for games on port %d", title.DiscoveryPort);
}

void TitleSystem::OnStop(World&)
{
	m_Discovery.Stop();
}

void TitleSystem::Update(World& world, float deltaTime)
{
	MatchStateComponent* state = GetState(world);
	if (!state || m_Screen == NullEntity || !world.IsValid(m_Screen))
		return;
	const TitleScreenComponent title = world.Get<TitleScreenComponent>(m_Screen);
	const double now = world.GetTime();

	// The match has started: fade through black into it, then the page goes and the mouse is the
	// game's again.
	if (state->Phase == MatchPhase::Countdown || state->Phase == MatchPhase::Racing || state->Phase == MatchPhase::Results)
	{
		ShowTruck(world, false, deltaTime);
		if (state->Phase == MatchPhase::Countdown)
			PreviewCab(world);
		Reveal(world, deltaTime);
		return;
	}
	ShowTruck(world, state->Phase == MatchPhase::Lobby || m_Joining, deltaTime);

	// What the player picked on the page.
	if (TitleRequestComponent* request = world.TryGet<TitleRequestComponent>(m_Screen); request && !request->Action.IsEmpty())
	{
		const String action = request->Action;
		const String address = request->Address;
		request->Action = String();
		if (state->Phase == MatchPhase::Title)
		{
			if (action == "host")
				Host(world, title);
			else if (action == "join" && !address.IsEmpty())
				Join(world, address);
			else if (action == "browse")
				m_Joining = true;
			else if (action == "back")
				m_Joining = false;
		}
		else if (action == "start" && GameNet::IsHost(world))
			state->StartRequested = true;
		else if (action == "back")
			Leave(world);
	}

	m_Discovery.Update(now);
	// A hosted game tells the LAN it is there until the match starts.
	if (m_HostedPort != 0 && state->Phase == MatchPhase::Lobby)
		m_Discovery.Announce(static_cast<uint16>(title.DiscoveryPort), m_HostedPort, CountPlayers(world), LanDiscovery::GetMachineName(), now);

	m_SincePage += deltaTime;
	if (m_SincePage >= PageInterval)
	{
		m_SincePage = 0;
		ShowPage(world);
	}
}

void TitleSystem::ShowTruck(World& world, bool on, float deltaTime)
{
	if (!world.IsValid(m_Truck) || !world.IsValid(m_Camera))
		return;
	TransformComponent& truck = world.Get<TransformComponent>(m_Truck);
	TransformComponent& camera = world.Get<TransformComponent>(m_Camera);
	if (!on)
	{
		if (m_ShowingTruck)
		{
			truck = m_TruckRest;
			camera = m_CameraRest;
			if (world.IsValid(m_Depot) && !m_DepotMesh.Mesh.IsEmpty())
				world.Add<MeshComponent>(m_Depot, m_DepotMesh);
			m_ShowingTruck = false;
		}
		return;
	}

	if (!m_ShowingTruck)
	{
		// Seen from its right, looking back at the middle of the hull.
		const Vec3 middle = m_TruckRest.Position + TruckMiddle * m_TruckRest.Scale.X;
		camera.Position = middle + ShowcaseOffset;
		const Vec3 target = middle + ShowcaseAim;
		const Vec3 look = target - camera.Position;
		const float flat = std::sqrt(look.X * look.X + look.Z * look.Z);
		camera.Rotation = { std::atan2(look.Y, flat) * RadToDeg, YawToward(camera.Position, target), 0 };
		if (world.IsValid(m_Depot) && world.Has<MeshComponent>(m_Depot))
		{
			m_DepotMesh = world.Get<MeshComponent>(m_Depot);
			world.Remove<MeshComponent>(m_Depot);
		}
		m_ShowingTruck = true;
		m_BobTime = 0;
	}

	// A slow float with a little pitch and roll, never quite in step, so it reads as idling.
	m_BobTime += deltaTime;
	const float t = m_BobTime * BobRate;
	truck.Position = m_TruckRest.Position + Vec3{ 0, BobHeight * std::sin(t), 0 };
	truck.Rotation = m_TruckRest.Rotation + Vec3{ BobPitch * std::sin(t * 0.8f + 0.6f), 0, BobRoll * std::sin(t * 0.6f + 1.9f) };
}

void TitleSystem::Reveal(World& world, float deltaTime)
{
	UIComponent& page = world.Get<UIComponent>(m_Screen);
	UIDocumentInstance* document = UISystem::For(world, m_Screen);
	switch (m_Fade)
	{
	case Fade::None:
		// Joiners get here without pressing Start; the host's page is already black.
		if (document)
		{
			document->Set("screen", "none");
			document->Set("fade", "black");
		}
		page.Input = false;
		m_Discovery.Stop();
		m_Fade = Fade::Black;
		m_FadeTime = 0;
		m_SteadyFrames = 0;
		break;
	case Fade::Black:
		m_FadeTime += std::min(deltaTime, SteadyFrameSeconds);
		m_SteadyFrames = deltaTime < SteadyFrameSeconds ? m_SteadyFrames + 1 : 0;
		if (m_FadeTime >= BlackAtLeast && m_SteadyFrames >= SteadyFramesNeeded)
		{
			if (document)
				document->Set("fade", "clear");
			Input::SetCursorMode(CursorMode::Locked);
			m_Fade = Fade::Clearing;
			m_FadeTime = 0;
		}
		break;
	case Fade::Clearing:
		m_FadeTime += deltaTime;
		if (m_FadeTime >= ClearSeconds)
			m_Fade = Fade::Done;
		break;
	case Fade::Done:
		break;
	}

	// The countdown in big numbers over the game, then GO, then the page goes.
	const MatchStateComponent* state = GetState(world);
	std::string label;
	if (state && state->Phase == MatchPhase::Countdown)
		label = std::to_string(std::max(1, static_cast<int>(std::ceil(state->Countdown))));
	else if (m_GoTime < GoSeconds)
	{
		label = "GO";
		m_GoTime += deltaTime;
	}
	if (label != m_CountdownLabel && document)
	{
		document->Set("countdown", label);
		m_CountdownLabel = label;
	}
	if (page.Visible && m_Fade == Fade::Done && label.empty())
		page.Visible = false;
}

void TitleSystem::PreviewCab(World& world)
{
	const Entity local = FindLocalMember(world);
	const CrewMemberComponent* member = local == NullEntity ? nullptr : world.TryGet<CrewMemberComponent>(local);
	const Entity body = world.FindEntityByName(CabBodyName);
	if (!member || member->Slot < 0 || member->Role != CrewRole::Driver || member->Crew != TruckCrew || !world.IsValid(body) || !world.IsValid(m_Camera))
		return;

	// The same look as walking the cab, so nothing changes at GO.
	const TransformComponent& stand = world.Get<TransformComponent>(body);
	StartLookComponent* look = world.TryGet<StartLookComponent>(local);
	if (!look)
	{
		look = &world.Add<StartLookComponent>(local);
		look->Yaw = stand.Rotation.Y;
	}
	const Vec2 turn = Input::GetMouseDelta() * m_LookSensitivity;
	look->Yaw -= turn.X;
	look->Pitch = std::clamp(look->Pitch - turn.Y, -89.0f, 89.0f);

	TransformComponent& camera = world.Get<TransformComponent>(m_Camera);
	camera.Position = stand.Position + Vec3{ 0, m_EyeHeight, 0 };
	camera.Rotation = { look->Pitch, look->Yaw, 0 };
}

void TitleSystem::Host(World& world, const TitleScreenComponent& title)
{
	NetworkSystem* network = FindNetwork(world);
	if (!network)
		network = world.AddSystem<NetworkSystem>();
	NetSession& session = network->GetSession();
	// Another game on this machine may hold the usual port; then any free one does, and the
	// announcement says which.
	if (!session.Start(static_cast<uint16>(title.GamePort)) && !session.Start(0))
	{
		m_Status = "Could not start a game here";
		return;
	}
	m_HostedPort = session.GetPort();
	m_Status = "Hosting on port " + std::to_string(m_HostedPort);
	GetState(world)->Phase = MatchPhase::Lobby;
	FP_LOG("Title: hosting a game on port %u", m_HostedPort);
}

void TitleSystem::Join(World& world, const String& address)
{
	NetAddress target;
	if (!NetAddress::Parse(address.CStr(), target, 7777))
	{
		m_Status = "That address does not look right";
		return;
	}
	NetworkSystem* network = FindNetwork(world);
	if (!network)
		network = world.AddSystem<NetworkSystem>();
	NetSession& session = network->GetSession();
	if (!session.Start(0))
	{
		m_Status = "Could not open the network";
		return;
	}
	session.Join(target);
	m_Status = "Joining " + target.ToString();
	GetState(world)->Phase = MatchPhase::Lobby;
	FP_LOG("Title: joining %s", target.ToString().c_str());
}

void TitleSystem::Leave(World& world)
{
	if (NetworkSystem* network = FindNetwork(world))
	{
		// Stopping says goodbye to everyone. Their players stay in this world until removed here,
		// and starting over makes the network take up this player afresh in the next game.
		network->OnStop(world);
		Array<Entity> others;
		for (auto [entity, shared] : world.View<NetworkComponent>().each())
		{
			if (shared.Remote)
				others.Add(entity);
		}
		for (Entity entity : others)
			world.DestroyEntity(entity);
		network->OnStart(world);
	}
	GameNet::SetHostId(0);
	m_HostedPort = 0;
	m_Joining = false;
	m_Status.clear();
	GetState(world)->Phase = MatchPhase::Title;
	FP_LOG("Title: left the game");
}

void TitleSystem::ShowPage(World& world)
{
	UIDocumentInstance* page = UISystem::For(world, m_Screen);
	const MatchStateComponent* state = GetState(world);
	if (!page || !state)
		return;

	const char* screen = state->Phase == MatchPhase::Lobby ? "lobby" : m_Joining ? "join" : "title";
	page->Set("screen", screen);
	page->Set("status", m_Status);
	page->Set("isHost", GameNet::IsHost(world));
	page->Set("players", CountPlayers(world));
	// Away from the host the lobby waits until the host has been heard from.
	page->Set("connected", GameNet::IsHost(world) || GameNet::GetHostId(world) != 0);

	std::string games = "[";
	for (const LanDiscovery::FoundGame& game : m_Discovery.GetGames())
	{
		char players[16];
		std::snprintf(players, sizeof(players), "%d", game.Players);
		if (games.size() > 1)
			games += ",";
		games += "{\"name\":" + Quote(game.Name) + ",\"address\":" + Quote(game.Address.ToString()) + ",\"players\":" + players + "}";
	}
	page->SetJson("games", games + "]");
}
