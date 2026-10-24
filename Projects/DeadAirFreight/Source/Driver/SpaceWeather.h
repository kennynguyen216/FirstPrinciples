#pragma once

#include "DriverSettings.h"

#include <string>
#include <vector>

struct TimedValue
{
	double Time = 0;
	float Value = 0;
};

struct Flyby
{
	std::string Name;
	float DiameterMeters = 0;
	float SpeedKmPerSecond = 0;
	float MissLunar = 0;
	double Time = 0;
};

double ParseUtc(const std::string& text);
std::string FormatDate(double time);

std::vector<TimedValue> ParseSolarWind(const std::string& json);
std::vector<TimedValue> ParseKp(const std::string& json);
std::vector<Flyby> ParseFlybys(const std::string& json);

struct AsteroidCluster
{
	std::string Name;
	float RouteFraction = 0;
	float OffsetAngle = 0;
	float Offset = 0;
	float RockSize = 0;
	float Drift = 0;
	float DiameterMeters = 0, SpeedKmPerSecond = 0, MissLunar = 0;
};

struct MatchPackage
{
	unsigned Seed = 1;
	std::vector<TimedValue> SolarWind;
	std::vector<TimedValue> Kp;
	std::vector<AsteroidCluster> Clusters;
	std::string WeatherLabel;
	std::string AsteroidLabel;
	std::string WindowDate;
	float PeakSolarWind = 0;
};

struct MatchSources
{
	std::vector<TimedValue> SolarWind, Kp;
	std::vector<Flyby> Flybys;
	bool WeatherLive = false, FlybysLive = false;
};

float NextRandom(unsigned& state);

float MapClamped(float value, float inMin, float inMax, float outMin, float outMax);

MatchPackage BuildMatchPackage(const MatchSources& sources, const DriverSettings& settings, unsigned seed);

float WindowHour(float matchTime, const DriverSettings& settings);
float SolarWindAt(const MatchPackage& package, float matchTime, const DriverSettings& settings);
float KpAt(const MatchPackage& package, float matchTime, const DriverSettings& settings);

std::string ToText(const MatchPackage& package);
bool FromText(const std::string& text, MatchPackage& out);
