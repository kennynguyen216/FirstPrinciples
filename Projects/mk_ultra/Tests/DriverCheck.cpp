// Self-check for the Driver's game logic. Needs no engine build. Run from the repo root (it reads the
// real settings file and the saved fallback bundle):
//   clang++ -std=c++20 -Isrc -Isrc/Core -IProjects/mk_ultra/Source Projects/mk_ultra/Tests/DriverCheck.cpp Projects/mk_ultra/Source/{Truck,Cab,SpaceWeather,Asteroids,LifeSupport,Tickets}.cpp -o /tmp/DriverCheck && /tmp/DriverCheck

#include "Asteroids.h"
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

// Engine headers take over assert, so check by hand.
#define CHECK(condition) if (!(condition)) { std::printf("FAILED line %d: %s\n", __LINE__, #condition); std::exit(1); }

static void CheckFlight()
{
	DriverSettings settings;
	settings.CruiseSpeed = 40;
	settings.ThrustAccel = 10;
	settings.BrakeDecel = 20;
	settings.MaxPitch = 80;
	settings.TurnTime90 = 1.5f;
	const SpaceWeatherNow calm{}; // all disturbance settings are 0, so no wobble or jerks
	const float dt = 1.0f / 60;

	// Thrust tops out at cruise speed, brake stops at zero.
	TruckState truck;
	truck.AtWheel = true;
	TruckControls controls;
	controls.Thrust = true;
	for (int i = 0; i < 600; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(std::abs(truck.Speed - settings.CruiseSpeed) < 1e-3f)
	controls = {};
	controls.Brake = true;
	for (int i = 0; i < 600; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Speed == 0)

	// The sliding semi: snap the nose 90 degrees while moving. Travel lags, then catches up in TurnTime90.
	truck = {};
	truck.AtWheel = true;
	truck.Speed = settings.CruiseSpeed;
	truck.Yaw = -90; // nose now points +X, travel still -Z
	StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.Travel.Z < -0.9f)
	for (int i = 0; i < 89; ++i) // 90 steps of 1/60 s = TurnTime90
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(Vec3::Dot(truck.Travel, GetNose(truck)) > 0.999f)

	// Pitch is clamped.
	truck = {};
	truck.AtWheel = true;
	controls = {};
	controls.Aim = { 0, -1 };
	for (int i = 0; i < 600; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Pitch == settings.MaxPitch)

	// Same seed and inputs fly the same path, jerks included.
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
	CHECK(a.Yaw != 0) // the disturbance actually moved the nose

	// Wheel left empty: controls do nothing, the truck holds steady for SteadyTime, then drifts.
	settings.SteadyTime = 6;
	settings.DriftRampTime = 15;
	truck = {};
	controls = {};
	controls.Thrust = true;
	controls.Aim = { 5, 5 };
	for (int i = 0; i < 5 * 60; ++i)
		StepTruck(truck, controls, calm, 1, settings, dt);
	CHECK(truck.Speed == 0 && truck.Yaw == 0 && truck.Pitch == 0)
	CHECK(GetDriftAmount(truck, settings) == 0)
	for (int i = 0; i < 10 * 60; ++i)
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.Yaw != 0) // drifting now
	for (int i = 0; i < 10 * 60; ++i)
		StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(GetDriftAmount(truck, settings) == 1)
	truck.AtWheel = true;
	StepTruck(truck, {}, calm, 1, settings, dt);
	CHECK(truck.WheelEmptyTime == 0)

	// On foot: walking forward goes -Z, and the cab walls stop you.
	settings.WalkSpeed = 3;
	settings.InteractRange = 2;
	DriverState driver;
	const CabBounds bounds{ { -3, 0, -5 }, { 3, 0, 5 } };
	for (int i = 0; i < 10 * 60; ++i)
		StepOnFoot(driver, {}, { 0, 1 }, bounds, settings, dt);
	CHECK(driver.Position.Z == -5 && driver.Position.X == 0)
	CHECK(CanReach(driver, { 0, 1, -6 }, settings))
	CHECK(!CanReach(driver, { 0, 1, 0 }, settings))
}

static std::string ReadFile(const char* path)
{
	std::ifstream file(path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

// The real tuning, read the same way the game reads it (LoadDriverSettings needs the engine).
static DriverSettings LoadRealSettings()
{
	DriverSettings settings;
	std::stringstream lines(ReadFile("Projects/mk_ultra/Content/Settings/Driver.settings"));
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
	CHECK(found > 50) // run from the repo root
	return settings;
}

static void CheckSpaceWeather(const DriverSettings& settings)
{
	CHECK(ParseUtc("1970-01-02T00:00:00") == 86400)
	CHECK(FormatDate(ParseUtc("2026-10-24T19:10:00Z")) == "2026-10-24")
	CHECK(FormatDate(ParseUtc("2024-02-29T23:59:59")) == "2024-02-29")

	// The saved bundle is real NOAA and NASA responses, so this checks the parsers on real data.
	MatchSources sources;
	sources.SolarWind = ParseSolarWind(ReadFile("Projects/mk_ultra/Content/SpaceWeather/Fallback/SolarWind.json"));
	sources.Kp = ParseKp(ReadFile("Projects/mk_ultra/Content/SpaceWeather/Fallback/Kp.json"));
	sources.Flybys = ParseFlybys(ReadFile("Projects/mk_ultra/Content/SpaceWeather/Fallback/Flybys.json"));
	CHECK(sources.SolarWind.size() > 500 && sources.SolarWind.front().Value > 100 && sources.SolarWind.back().Value < 3000)
	CHECK(!sources.Kp.empty() && sources.Kp.back().Value >= 0 && sources.Kp.back().Value <= 9)
	CHECK(sources.Flybys.size() == 13 && !sources.Flybys.front().Name.empty() && sources.Flybys.front().DiameterMeters > 0)
	CHECK(sources.Flybys.front().SpeedKmPerSecond > 0 && sources.Flybys.front().MissLunar > 0 && sources.Flybys.front().Time > 0)

	const MatchPackage package = BuildMatchPackage(sources, settings, 7);
	CHECK(package.WeatherLabel == "REPLAY " + package.WindowDate) // fallback data is never labelled live
	CHECK(!package.SolarWind.empty() && package.SolarWind.front().Time >= 0 && package.SolarWind.back().Time <= settings.WeatherWindowHours + 0.01f)
	CHECK(package.PeakSolarWind >= SolarWindAt(package, settings.MatchLength * 0.5f, settings))
	CHECK(KpAt(package, 0, settings) >= 0 && KpAt(package, settings.MatchLength, settings) <= 9)
	CHECK(package.Clusters.size() == sources.Flybys.size())
	bool blocked = false;
	for (const AsteroidCluster& cluster : package.Clusters)
		blocked |= cluster.Offset == 0;
	CHECK(blocked) // the straight line is always blocked somewhere

	// Both trucks must rebuild the same package from its text.
	MatchPackage copy;
	CHECK(FromText(ToText(package), copy))
	CHECK(ToText(copy) == ToText(package))
	MatchPackage timeSeeded = package;
	timeSeeded.Seed = 1793000123; // match seeds come from the clock, so they're 10 digits
	MatchPackage timeSeededCopy;
	CHECK(FromText(ToText(timeSeeded), timeSeededCopy) && timeSeededCopy.Seed == timeSeeded.Seed)
	MatchPackage broken;
	CHECK(!FromText("nonsense line", broken))

	// Same package, same rocks.
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
	truck.Position = { 0, 0, -3 }; // inside rock radius + truck radius
	truck.Speed = 40;
	truck.Travel = { 0, 0, -1 };   // flying into it
	CHECK(CollideWithRocks(truck, { rock }, 0, settings))
	CHECK(truck.Speed < 40 * settings.BounceSpeedKeep + 1e-3f)
	CHECK(truck.Travel.Z > 0) // bounced back
	CHECK((truck.Position - rock.Center).Length() >= rock.Radius + settings.TruckRadius - 1e-3f)
	CHECK(!CollideWithRocks(truck, { rock }, 0, settings))
}

static void CheckLifeSupport(const DriverSettings& settings)
{
	const float dt = 1.0f / 60;
	LifeSupport life = StartLifeSupport(settings);
	CHECK(std::abs(GetTimeCompression(settings) - 288) < 1) // 24 h into 5 min
	CHECK(life.ColonyTimeLeft > settings.CrossingTime && life.ColonyTimeLeft < settings.MatchLength * 1.5f)

	// The cab reserve runs out first, then the cargo pays.
	for (int i = 0; i < static_cast<int>(settings.CabReserveTime * 60) - 10; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.CabReserve > 0 && life.Cargo == settings.StartingCargo)
	for (int i = 0; i < 120; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.CabReserve == 0 && life.Cargo < settings.StartingCargo)

	// Broken scrubber: CO2 climbs to a blackout, then the Driver wakes with it still high.
	BreakScrubber(life);
	CHECK(life.ScrubberBroken)
	for (int i = 0; i < static_cast<int>(settings.Co2RiseTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(IsBlackedOut(life))
	CHECK(GetDisturbanceScale(life, settings) > 1) // woozy
	for (int i = 0; i < static_cast<int>(settings.BlackoutTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(!IsBlackedOut(life) && life.Co2 <= settings.Co2AfterBlackout + 0.05f)

	// Holding E for the repair time fixes it, and CO2 clears.
	for (int i = 0; i < static_cast<int>(settings.ScrubberRepairTime * 60) + 5; ++i)
		StepLifeSupport(life, true, settings, dt);
	CHECK(!life.ScrubberBroken)
	for (int i = 0; i < static_cast<int>(settings.Co2FallTime * 60) + 5; ++i)
		StepLifeSupport(life, false, settings, dt);
	CHECK(life.Co2 == 0)

	// A banked Free Fix eats the next break.
	life.AutoFix = true;
	BreakScrubber(life);
	CHECK(!life.ScrubberBroken && !life.AutoFix)
}

static void CheckTickets(const DriverSettings& settings)
{
	const std::vector<float> odds = GetPrizeOdds(settings);
	CHECK(RollPrize(0, odds) == Prize::Dud)
	CHECK(RollPrize(0.999f, odds) == Prize::Jackpot)
	// On average tickets lose a little air: expected refund is less than the cost.
	CHECK(settings.PrizeJackpot * settings.JackpotTickets < 1)

	LifeSupport life = StartLifeSupport(settings);
	Ticket ticket;
	CHECK(BuyTicket(ticket, life, Prize::Jackpot, settings))
	CHECK(std::abs(life.Cargo - settings.StartingCargo * (1 - settings.TicketCostFraction)) < 1e-3f)
	CHECK(!BuyTicket(ticket, life, Prize::Dud, settings)) // one at a time

	// Scratch one row: not enough to reveal. Then sweep the whole ticket.
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

	// Free Fix repairs a broken scrubber on the spot.
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
	CheckLifeSupport(settings);
	CheckTickets(settings);
	std::printf("DriverCheck passed\n");
}
