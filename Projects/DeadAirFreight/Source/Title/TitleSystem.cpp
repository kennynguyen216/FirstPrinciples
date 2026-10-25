#include "Title/TitleSystem.h"
#include "Engine/Net/NetworkComponent.h"
#include "Engine/Net/NetworkSystem.h"
#include "Engine/UI/UISystem.h"
#include "Match/MatchQueries.h"
#include "Net/GameNet.h"

#include <cstdio>

using namespace FP;
using namespace MatchQueries;

namespace
{
	// How often the page hears the lobby and the games found.
	constexpr float PageInterval = 0.25f;

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

	// The match has started: the page goes and the mouse is the game's again.
	UIComponent& page = world.Get<UIComponent>(m_Screen);
	if (state->Phase == MatchPhase::Countdown || state->Phase == MatchPhase::Racing || state->Phase == MatchPhase::Results)
	{
		if (page.Visible)
		{
			page.Visible = false;
			page.Input = false;
			Input::SetCursorMode(CursorMode::Locked);
			m_Discovery.Stop();
		}
		return;
	}

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
