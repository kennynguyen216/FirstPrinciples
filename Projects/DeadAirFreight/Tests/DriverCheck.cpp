#include "Asteroids.h"
#include "BlackHoles.h"
#include "Cab.h"
#include "LifeSupport.h"
#include "SpaceWeather.h"
#include "Tickets.h"
#include "Truck.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace FP;

#define CHECK(condition) if (!(condition)) { std::printf("FAILED line %d: %s\n", __LINE__, #condition); std::exit(1); }

static void CheckFlight()
{
	DriverSettings settings;
	settings.CruiseSpeed = 40;
	settings.ThrustAccel = 10;
	settings.MaxPitch = 80;
	settings.TurnTime90 = 1.5f;
	const SpaceWeatherNow calm{};
	const float dt = 1.0f / 60;

	TruckState truck;
	TruckControls controls;
	for (int i = 0; i < 600; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(std::abs(truck.Speed - settings.CruiseSpeed) < 1e-3f)

	settings.BoostSpeedScale = 2.5f;
	settings.BoostAccel = 80;
	truck.Boost = 3;
	for (int i = 0; i < 2 * 60; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(std::abs(truck.Speed - settings.CruiseSpeed * settings.BoostSpeedScale) < 1e-3f)
	for (int i = 0; i < 20 * 60; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Boost == 0 && std::abs(truck.Speed - settings.CruiseSpeed) < 1e-3f)

	settings.BoostJerkInterval = 0.5f;
	settings.BoostJerkStrength = 70;
	settings.JerkDecay = 5;
	truck = {};
	truck.AtWheel = true;
	truck.Boost = 3;
	for (int i = 0; i < 60; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Yaw != 0 || truck.Pitch != 0)

	for (const float startYaw : { 10.0f, -10.0f })
	{
		for (int jerk = 0; jerk < 20; ++jerk)
		{
			truck = {};
			truck.Destination = { 0, 0, -1000 };
			truck.Yaw = startYaw;
			truck.Pitch = 5;
			truck.Boost = 3;
			truck.Random = 1 + jerk;
			truck.BoostJerkSide = jerk % 2 ? 1 : -1;
			StepDisturbance(truck, calm, settings, 1e-4f);
			CHECK(truck.JerkRate.X * startYaw + truck.JerkRate.Y * truck.Pitch > 0)
		}
	}
	settings.JerkDecay = 0;

	truck = {};
	truck.AtWheel = true;
	truck.Speed = settings.CruiseSpeed;
	truck.Yaw = -90;
	StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.Travel.Z < -0.9f)
	for (int i = 0; i < 89; ++i)
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(Vec3::Dot(truck.Travel, GetNose(truck)) > 0.999f)

	truck = {};
	truck.AtWheel = true;
	controls = {};
	controls.Aim = { 0, -1 };
	for (int i = 0; i < 600; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Pitch == settings.MaxPitch)

	settings.CalmJerkInterval = settings.StormJerkInterval = 1;
	settings.JerkStrength = 90;
	settings.JerkDecay = 5;
	settings.CalmWobble = settings.StormWobble = 10;
	settings.WobbleFrequency = 0.3f;
	TruckState a, b;
	a.AtWheel = b.AtWheel = true;
	a.Speed = b.Speed = 20;
	for (int i = 0; i < 600; ++i)
	{
		StepTruck(a, {}, calm, 1, settings, dt);
		StepTruck(b, {}, calm, 1, settings, dt);
	}
	CHECK(a.Yaw == b.Yaw && a.Pitch == b.Pitch && a.Position.X == b.Position.X)
	CHECK(a.Yaw != 0)

	settings.SteadyTime = 6;
	settings.DriftRampTime = 15;
	truck = {};
	controls = {};
	controls.Aim = { 5, 5 };
	for (int i = 0; i < 5 * 60; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Yaw == 0 && truck.Pitch == 0)
	CHECK(GetDriftAmount(truck, settings) == 0)
	for (int i = 0; i < 10 * 60; ++i)
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.Yaw != 0)
	for (int i = 0; i < 10 * 60; ++i)
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(GetDriftAmount(truck, settings) == 1)
	truck.AtWheel = true;
	StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.WheelEmptyTime == 0)

	settings.WalkSpeed = 3;
	settings.InteractRange = 2;
	DriverState driver;
	const CabBounds bounds{ { -3, 0, -5 }, { 3, 0, 5 } };
	for (int i = 0; i < 10 * 60; ++i)
		StepOnFoot(driver, {}, { 0, 1 }, bounds, settings, dt);
	CHECK(driver.Position.Z == -5 && driver.Position.X == 0)
	CHECK(CanReach(driver, { 0, 1, -6 }, settings))
	CHECK(!CanReach(driver, { 0, 1, 0 }, settings))

	// The back wall's doorway lets the walker past the wall only inside its width, and keeps them in it.
	CabBounds door = bounds;
	door.DoorHalfWidth = 0.5f;
	door.DoorZ = 6;
	driver.Position = { 2, 0, 0 };
	for (int i = 0; i < 10 * 60; ++i)
		StepOnFoot(driver, {}, { 0, -1 }, door, settings, dt);
	CHECK(driver.Position.Z == 5)
	driver.Position = { 0, 0, 0 };
	for (int i = 0; i < 10 * 60; ++i)
		StepOnFoot(driver, {}, { 0, -1 }, door, settings, dt);
	CHECK(driver.Position.Z == 6)
	for (int i = 0; i < 60; ++i)
		StepOnFoot(driver, {}, { 1, 0 }, door, settings, dt);
	CHECK(driver.Position.X == 0.5f)
}

static std::string ReadFile(const char* path)
{
	std::ifstream file(path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

static DriverSettings LoadRealSettings()
{
	DriverSettings settings;
	std::stringstream lines(ReadFile("Projects/DeadAirFreight/Content/Settings/Driver.settings"));
	std::string line;
	int found = 0;
	while (std::getline(lines, line))
	{
		std::stringstream words(line.substr(0, line.find('#')));
		std::string name;
		float value = 0;
		if (!(words >> name >> value))
			continue;
#define DRIVER_SETTING_READ(field) if (name == #field) { settings.field = value; ++found; }
		DRIVER_SETTINGS(DRIVER_SETTING_READ)
#undef DRIVER_SETTING_READ
	}
	CHECK(found > 50)
	return settings;
}

static void CheckSpaceWeather(const DriverSettings& settings)
{
	CHECK(ParseUtc("1970-01-02T00:00:00") == 86400)
	CHECK(FormatDate(ParseUtc("2026-10-24T19:10:00Z")) == "2026-10-24")
	CHECK(FormatDate(ParseUtc("2024-02-29T23:59:59")) == "2024-02-29")

	MatchSources sources;
	sources.SolarWind = ParseSolarWind(ReadFile("Projects/DeadAirFreight/Content/SpaceWeather/Fallback/SolarWind.json"));
	sources.Kp = ParseKp(ReadFile("Projects/DeadAirFreight/Content/SpaceWeather/Fallback/Kp.json"));
	sources.Flybys = ParseFlybys(ReadFile("Projects/DeadAirFreight/Content/SpaceWeather/Fallback/Flybys.json"));
	CHECK(sources.SolarWind.size() > 500 && sources.SolarWind.front().Value > 100 && sources.SolarWind.back().Value < 3000)
	CHECK(!sources.Kp.empty() && sources.Kp.back().Value >= 0 && sources.Kp.back().Value <= 9)
	CHECK(sources.Flybys.size() == 13 && !sources.Flybys.front().Name.empty() && sources.Flybys.front().DiameterMeters > 0)
	CHECK(sources.Flybys.front().SpeedKmPerSecond > 0 && sources.Flybys.front().MissLunar > 0 && sources.Flybys.front().Time > 0)

	const MatchPackage package = BuildMatchPackage(sources, settings, 7);
	CHECK(package.WeatherLabel == "REPLAY " + package.WindowDate)
	CHECK(!package.SolarWind.empty() && package.SolarWind.front().Time >= 0 && package.SolarWind.back().Time <= settings.WeatherWindowHours + 0.01f)
	CHECK(package.PeakSolarWind >= SolarWindAt(package, settings.MatchLength * 0.5f, settings))
	CHECK(KpAt(package, 0, settings) >= 0 && KpAt(package, settings.MatchLength, settings) <= 9)
	CHECK(package.Clusters.size() == sources.Flybys.size())
	bool blocked = false;
	for (const AsteroidCluster& cluster : package.Clusters)
		blocked |= cluster.Offset == 0;
	CHECK(blocked)

	MatchPackage copy;
	CHECK(FromText(ToText(package), copy))
	CHECK(ToText(copy) == ToText(package))
	MatchPackage timeSeeded = package;
	timeSeeded.Seed = 1793000123;
	MatchPackage timeSeededCopy;
	CHECK(FromText(ToText(timeSeeded), timeSeededCopy) && timeSeededCopy.Seed == timeSeeded.Seed)
	MatchPackage broken;
	CHECK(!FromText("nonsense line", broken))

	const Route route{ {}, { 0, 0, -1 }, settings.CruiseSpeed * settings.CrossingTime };
	const std::vector<Rock> a = BuildRocks(package, route, settings);
	const std::vector<Rock> b = BuildRocks(copy, route, settings);
	CHECK(!a.empty() && a.size() == b.size())
	CHECK((GetRockPosition(a[3], 42) - GetRockPosition(b[3], 42)).Length() < 1e-3f)
}

static void CheckAsteroidBounce(const DriverSettings& settings)
{
	Rock rock;
	rock.Center = { 0, 0, -10 };
	rock.Axis = { 0, 1, 0 };
	rock.Radius = 5;
	TruckState truck;
	truck.Position = { 0, 0, -3 };
	truck.Speed = 40;
	truck.Travel = { 0, 0, -1 };
	CHECK(CollideWithRocks(truck, { rock }, 0, settings))
	CHECK(truck.Speed < 40 * settings.BounceSpeedKeep + 1e-3f)
	CHECK(truck.Travel.Z > 0)
	CHECK((truck.Position - rock.Center).Length() >= rock.Radius + settings.TruckRadius - 1e-3f)
	CHECK(!CollideWithRocks(truck, { rock }, 0, settings))
}

static void CheckBlackHoles(const DriverSettings& settings)
{
	BlackHole hole;
	hole.Position = { 0, 0, -100 };
	hole.CoreRadius = settings.BlackHoleCoreRadius;
	hole.RingRadius = settings.BlackHoleRingRadius;
	std::vector<BlackHole> holes{ hole };
	TruckState truck;
	truck.Position = hole.Position + Vec3{ (hole.CoreRadius + settings.TruckRadius + hole.RingRadius) / 2, 0, 0 };
	truck.Travel = { 0, 0, -1 };
	bool slung = false;
	for (int i = 0; i < 60 * (settings.SlingshotTime * 2 + 1); ++i)
	{
		const BlackHoleEvent event = StepBlackHoles(truck, holes, settings, 1.0f / 60);
		CHECK(event != BlackHoleEvent::Swallowed)
		CHECK(!(slung && event == BlackHoleEvent::Slingshot))
		slung = slung || event == BlackHoleEvent::Slingshot;
	}
	CHECK(slung && truck.Boost == settings.BoostTime && holes[0].Spent)
	CHECK(truck.Travel.X < 0)
	truck.Position = hole.Position + Vec3{ hole.RingRadius + 1, 0, 0 };
	StepBlackHoles(truck, holes, settings, 1.0f / 60);
	CHECK(holes[0].Charge == 0)

	truck.Position = hole.Position;
	truck.Speed = 40;
	CHECK(StepBlackHoles(truck, holes, settings, 1.0f / 60) == BlackHoleEvent::Swallowed)
	CHECK((truck.Position - hole.Position).Length() >= hole.CoreRadius + settings.TruckRadius - 1e-3f)
}

static void CheckLifeSupport(const DriverSettings& settings)
{
	const float dt = 1.0f / 60;
	LifeSupport life = StartLifeSupport(settings);
	CHECK(std::abs(GetTimeCompression(settings) - 288) < 1)
	CHECK(life.ColonyTimeLeft > settings.CrossingTime && life.ColonyTimeLeft < settings.MatchLength * 1.5f)

	for (int i = 0; i < static_cast<int>(settings.CabReserveTime * 60) - 10; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.CabReserve > 0 && life.Cargo == settings.StartingCargo)
	for (int i = 0; i < 120; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.CabReserve == 0 && life.Cargo < settings.StartingCargo)

	BreakScrubber(life);
	CHECK(life.ScrubberBroken)
	for (int i = 0; i < static_cast<int>(settings.Co2RiseTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(IsBlackedOut(life))
	CHECK(GetDisturbanceScale(life, settings) > 1)
	for (int i = 0; i < static_cast<int>(settings.BlackoutTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(!IsBlackedOut(life) && life.Co2 <= settings.Co2AfterBlackout + 0.05f)

	for (int i = 0; i < static_cast<int>(settings.ScrubberRepairTime * 60) + 5; ++i)
		StepLifeSupport(life, true, settings, dt);
	CHECK(!life.ScrubberBroken)
	for (int i = 0; i < static_cast<int>(settings.Co2FallTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.Co2 == 0)

	life.AutoFix = true;
	BreakScrubber(life);
	CHECK(!life.ScrubberBroken && !life.AutoFix)
}

static void CheckTickets(const DriverSettings& settings)
{
	const std::vector<float> odds = GetPrizeOdds(settings);
	CHECK(RollPrize(0, odds) == Prize::Dud)
	CHECK(RollPrize(0.999f, odds) == Prize::Jackpot)
	CHECK(WithPity(Prize::Dud, static_cast<int>(settings.JackpotPity) - 2, settings) == Prize::Dud)
	CHECK(WithPity(Prize::Dud, static_cast<int>(settings.JackpotPity) - 1, settings) == Prize::Jackpot)
	// Even with pity, tickets lose air on average: the jackpot rate is one over the expected
	// tickets until one, which pity cuts off at JackpotPity.
	const float jackpotOdds = settings.PrizeJackpot / (settings.PrizeDud + settings.PrizeCalmHands + settings.PrizeFreeFix + settings.PrizeJackpot);
	const float ticketsPerJackpot = (1 - std::pow(1 - jackpotOdds, settings.JackpotPity)) / jackpotOdds;
	CHECK(settings.JackpotTickets / ticketsPerJackpot < 1)

	LifeSupport life = StartLifeSupport(settings);
	Ticket ticket;
	CHECK(BuyTicket(ticket, life, Prize::Jackpot, settings))
	CHECK(std::abs(life.Cargo - settings.StartingCargo * (1 - settings.TicketCostFraction)) < 1e-3f)
	CHECK(!BuyTicket(ticket, life, Prize::Dud, settings))

	CHECK(!StepTicket(ticket, life, {}, true, settings, 0.016f))
	CHECK(GetScratchedFraction(ticket) > 0 && GetScratchedFraction(ticket) < settings.TicketRevealFraction)
	bool revealed = false;
	ticket.Coin = { 0, 0 };
	for (int y = 0; y <= ticket.Rows && !revealed; ++y)
	{
		for (int x = 0; x <= ticket.Columns && !revealed; ++x)
		{
			ticket.Coin = { static_cast<float>(x), static_cast<float>(y) };
			revealed = StepTicket(ticket, life, {}, true, settings, 0.016f);
		}
	}
	CHECK(revealed && ticket.Revealed)
	CHECK(std::abs(life.Cargo - settings.StartingCargo * (1 + settings.TicketCostFraction * (settings.JackpotTickets - 1))) < 1e-3f)

	life.ScrubberBroken = true;
	ApplyPrize(Prize::FreeFix, life, settings);
	CHECK(!life.ScrubberBroken)
}

int main()
{
	CheckFlight();
	const DriverSettings settings = LoadRealSettings();
	CheckSpaceWeather(settings);
	CheckAsteroidBounce(settings);
	CheckBlackHoles(settings);
	CheckLifeSupport(settings);
	CheckTickets(settings);
	std::printf("DriverCheck passed\n");
}
