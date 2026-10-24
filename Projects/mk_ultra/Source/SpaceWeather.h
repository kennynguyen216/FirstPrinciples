#pragma once

#include "DriverSettings.h"

#include <string>
#include <vector>

// Real space weather turned into the Match Package: the one bundle both trucks play from, so they
// get the same weather and the same asteroids. Plain C++ with no engine types, so
// it can be tested alone and sent between machines as text (ToText / FromText).

// ---- Raw readings, parsed from the live feeds or the fallback bundle (same formats) ----

struct TimedValue
{
	double Time = 0; // seconds since 1970 UTC
	float Value = 0;
};

struct Flyby
{
	std::string Name;
	float DiameterMeters = 0; // NASA's max estimate
	float SpeedKmPerSecond = 0;
	float MissLunar = 0;      // miss distance in lunar distances
	double Time = 0;          // close approach, seconds since 1970 UTC
};

// "2026-10-24T19:10:00" (a trailing Z or fraction is ignored) to seconds since 1970 UTC. 0 if unreadable.
double ParseUtc(const std::string& text);
std::string FormatDate(double time); // "2026-10-24"

std::vector<TimedValue> ParseSolarWind(const std::string& json); // NOAA rtsw_wind_1m.json, active readings only
std::vector<TimedValue> ParseKp(const std::string& json);        // NOAA noaa-planetary-k-index.json
std::vector<Flyby> ParseFlybys(const std::string& json);         // NASA NeoWs feed

// ---- The Match Package ----------------------------------------------------

struct AsteroidCluster
{
	std::string Name;       // the real asteroid it stands for
	float RouteFraction = 0; // 0 depot .. 1 colony, where along the route the cluster sits
	float OffsetAngle = 0;   // degrees around the route
	float Offset = 0;        // units from the straight line
	float RockSize = 0;      // units
	float Drift = 0;         // units/s the rocks move
	float DiameterMeters = 0, SpeedKmPerSecond = 0, MissLunar = 0; // the real numbers, for the results screen
};

struct MatchPackage
{
	unsigned Seed = 1;
	std::vector<TimedValue> SolarWind; // Time is hours into the replayed window
	std::vector<TimedValue> Kp;        // same
	std::vector<AsteroidCluster> Clusters;
	std::string WeatherLabel;  // "LIVE" or "REPLAY 2026-10-24"
	std::string AsteroidLabel; // same idea for the flybys
	std::string WindowDate;    // date of the replayed window's end
	float PeakSolarWind = 0;   // km/s over the window
};

struct MatchSources
{
	std::vector<TimedValue> SolarWind, Kp;
	std::vector<Flyby> Flybys;
	bool WeatherLive = false, FlybysLive = false;
};

// Deterministic 0 to 1, so both machines roll the same numbers from the same seed.
float NextRandom(unsigned& state);

// value's place between inMin and inMax, clamped, carried over to outMin..outMax.
float MapClamped(float value, float inMin, float inMax, float outMin, float outMax);

// Replays the last WeatherWindowHours before the newest solar wind reading across MatchLength.
MatchPackage BuildMatchPackage(const MatchSources& sources, const DriverSettings& settings, unsigned seed);

// Hours into the window at this point of the match: the timelapse.
float WindowHour(float matchTime, const DriverSettings& settings);
float SolarWindAt(const MatchPackage& package, float matchTime, const DriverSettings& settings); // interpolated
float KpAt(const MatchPackage& package, float matchTime, const DriverSettings& settings);        // 3-hourly steps

// For sending the package to the other truck. FromText returns false on a malformed package.
std::string ToText(const MatchPackage& package);
bool FromText(const std::string& text, MatchPackage& out);
