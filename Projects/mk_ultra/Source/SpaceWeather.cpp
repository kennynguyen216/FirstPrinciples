#include "SpaceWeather.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace
{
	// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
	long long DaysFromCivil(int y, int m, int d)
	{
		y -= m <= 2;
		const long long era = (y >= 0 ? y : y - 399) / 400;
		const int yoe = y - static_cast<int>(era * 400);
		const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + doe - 719468;
	}

	// Reads the value of "key" somewhere in [pos, end): a quoted string, or a bare number/true/false/null.
	// Moves pos past the value.
	bool ReadField(const std::string& json, size_t& pos, size_t end, const char* key, std::string& value)
	{
		const std::string quoted = std::string("\"") + key + "\"";
		size_t at = json.find(quoted, pos);
		if (at == std::string::npos || at >= end)
			return false;
		at = json.find(':', at + quoted.size());
		if (at == std::string::npos || at >= end)
			return false;
		for (++at; at < end && std::isspace(static_cast<unsigned char>(json[at])); ++at)
		{
		}
		if (at < end && json[at] == '"')
		{
			const size_t close = json.find('"', at + 1);
			if (close == std::string::npos)
				return false;
			value = json.substr(at + 1, close - at - 1);
			pos = close + 1;
			return true;
		}
		const size_t stop = std::min(json.find_first_of(",}]", at), end);
		value = json.substr(at, stop - at);
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
			value.pop_back();
		pos = stop;
		return true;
	}

	// Calls read(begin, end) for each flat {...} object in a JSON array.
	template <typename Read>
	void ForEachFlatObject(const std::string& json, Read read)
	{
		for (size_t open = json.find('{'); open != std::string::npos; open = json.find('{', open + 1))
		{
			const size_t close = json.find('}', open);
			if (close == std::string::npos)
				return;
			read(open, close);
			open = close;
		}
	}

	// Calls read(begin, end) for each object nested exactly depth levels inside the value that starts at from.
	// Skips braces inside strings, so key order and formatting don't matter.
	template <typename Read>
	void ForEachObjectAtDepth(const std::string& json, size_t from, int depth, Read read)
	{
		int level = 0;
		bool inString = false;
		size_t open = 0;
		for (size_t i = from; i < json.size(); ++i)
		{
			const char c = json[i];
			if (inString)
			{
				if (c == '\\')
					++i;
				else if (c == '"')
					inString = false;
				continue;
			}
			if (c == '"')
				inString = true;
			else if (c == '{' || c == '[')
			{
				if (++level == depth && c == '{')
					open = i;
			}
			else if (c == '}' || c == ']')
			{
				if (level == depth && c == '}')
					read(open, i);
				if (--level == 0)
					return;
			}
		}
	}

	bool IsNumber(const std::string& text, float& out)
	{
		char* end = nullptr;
		out = std::strtof(text.c_str(), &end);
		return !text.empty() && end != text.c_str();
	}

	std::vector<std::string> SplitTabs(const std::string& line)
	{
		std::vector<std::string> parts;
		std::stringstream stream(line);
		std::string part;
		while (std::getline(stream, part, '\t'))
			parts.push_back(part);
		return parts;
	}

	std::string Number(double value)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.9g", value);
		return text;
	}
}

double ParseUtc(const std::string& text)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
	if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3)
		return 0;
	return static_cast<double>(DaysFromCivil(y, mo, d)) * 86400.0 + h * 3600.0 + mi * 60.0 + s;
}

std::string FormatDate(double time)
{
	// Civil date from days (inverse of DaysFromCivil).
	long long z = static_cast<long long>(std::floor(time / 86400.0)) + 719468;
	const long long era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	const long long y = static_cast<long long>(yoe) + era * 400 + (m <= 2);
	char text[16];
	std::snprintf(text, sizeof(text), "%04lld-%02u-%02u", y, m, d);
	return text;
}

std::vector<TimedValue> ParseSolarWind(const std::string& json)
{
	std::vector<TimedValue> values;
	ForEachFlatObject(json, [&](size_t open, size_t close) {
		std::string time, active, speed;
		size_t a = open, b = open, c = open;
		float value = 0;
		if (ReadField(json, a, close, "time_tag", time) && ReadField(json, b, close, "active", active) && active == "true"
			&& ReadField(json, c, close, "proton_speed", speed) && IsNumber(speed, value) && ParseUtc(time) > 0)
			values.push_back({ ParseUtc(time), value });
	});
	std::sort(values.begin(), values.end(), [](const TimedValue& x, const TimedValue& y) { return x.Time < y.Time; });
	return values;
}

std::vector<TimedValue> ParseKp(const std::string& json)
{
	std::vector<TimedValue> values;
	ForEachFlatObject(json, [&](size_t open, size_t close) {
		std::string time, kp;
		size_t a = open, b = open;
		float value = 0;
		if (ReadField(json, a, close, "time_tag", time) && ReadField(json, b, close, "Kp", kp) && IsNumber(kp, value) && ParseUtc(time) > 0)
			values.push_back({ ParseUtc(time), value });
	});
	std::sort(values.begin(), values.end(), [](const TimedValue& x, const TimedValue& y) { return x.Time < y.Time; });
	return values;
}

std::vector<Flyby> ParseFlybys(const std::string& json)
{
	// near_earth_objects is { "date": [ {asteroid}, ... ], ... }, so each asteroid sits 3 levels in.
	static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	std::vector<Flyby> flybys;
	const size_t list = json.find("\"near_earth_objects\"");
	if (list == std::string::npos)
		return flybys;
	ForEachObjectAtDepth(json, json.find('{', list), 3, [&](size_t open, size_t close) {
		std::string name, diameter, approach, speed, lunar;
		size_t namePos = open, approachPos = open, speedPos = open, lunarPos = open;
		size_t metersPos = json.find("\"meters\"", open); // the diameter in meters, not feet or miles
		if (metersPos > close || !ReadField(json, namePos, close, "name", name) || !ReadField(json, metersPos, close, "estimated_diameter_max", diameter)
			|| !ReadField(json, approachPos, close, "close_approach_date_full", approach) || !ReadField(json, speedPos, close, "kilometers_per_second", speed)
			|| !ReadField(json, lunarPos, close, "lunar", lunar))
			return;
		Flyby flyby;
		flyby.Name = name;
		IsNumber(diameter, flyby.DiameterMeters);
		IsNumber(speed, flyby.SpeedKmPerSecond);
		IsNumber(lunar, flyby.MissLunar);
		// "2026-Oct-24 21:10"
		int y = 0, d = 0, h = 0, mi = 0;
		char month[4] = {};
		if (std::sscanf(approach.c_str(), "%d-%3s-%d %d:%d", &y, month, &d, &h, &mi) == 5)
		{
			for (int m = 0; m < 12; ++m)
			{
				if (std::string(month) == months[m])
					flyby.Time = static_cast<double>(DaysFromCivil(y, m + 1, d)) * 86400.0 + h * 3600.0 + mi * 60.0;
			}
		}
		flybys.push_back(flyby);
	});
	return flybys;
}

float NextRandom(unsigned& state)
{
	unsigned x = state ? state : 1;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	state = x;
	return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

float MapClamped(float value, float inMin, float inMax, float outMin, float outMax)
{
	const float t = inMax > inMin ? std::clamp((value - inMin) / (inMax - inMin), 0.0f, 1.0f) : 0.0f;
	return outMin + (outMax - outMin) * t;
}

MatchPackage BuildMatchPackage(const MatchSources& sources, const DriverSettings& settings, unsigned seed)
{
	MatchPackage package;
	package.Seed = seed;
	unsigned random = seed;

	const double end = sources.SolarWind.empty() ? 0 : sources.SolarWind.back().Time;
	const double start = end - settings.WeatherWindowHours * 3600.0;
	auto hour = [&](double time) { return static_cast<float>((time - start) / 3600.0); };
	package.WindowDate = FormatDate(end);
	package.WeatherLabel = sources.WeatherLive ? "LIVE" : "REPLAY " + package.WindowDate;

	for (const TimedValue& wind : sources.SolarWind)
	{
		if (wind.Time >= start)
		{
			package.SolarWind.push_back({ hour(wind.Time), wind.Value });
			package.PeakSolarWind = std::max(package.PeakSolarWind, wind.Value);
		}
	}
	// Kp is 3-hourly, so keep the reading in force when the window opens.
	for (const TimedValue& kp : sources.Kp)
	{
		if (kp.Time >= start - 3 * 3600.0 && kp.Time <= end)
			package.Kp.push_back({ hour(kp.Time), kp.Value });
	}

	// Each real flyby becomes a cluster. Keep the closest passes, then lay them out in flyby order.
	std::vector<Flyby> flybys = sources.Flybys;
	std::sort(flybys.begin(), flybys.end(), [](const Flyby& x, const Flyby& y) { return x.MissLunar < y.MissLunar; });
	if (flybys.size() > static_cast<size_t>(settings.MaxClusters))
		flybys.resize(static_cast<size_t>(settings.MaxClusters));
	const std::string closest = flybys.empty() ? "" : flybys.front().Name;
	std::sort(flybys.begin(), flybys.end(), [](const Flyby& x, const Flyby& y) { return x.Time < y.Time; });
	for (size_t i = 0; i < flybys.size(); ++i)
	{
		const Flyby& flyby = flybys[i];
		AsteroidCluster cluster;
		cluster.Name = flyby.Name;
		cluster.RouteFraction = settings.RouteClusterStart + (settings.RouteClusterEnd - settings.RouteClusterStart) * (static_cast<float>(i) + 0.5f) / static_cast<float>(flybys.size());
		cluster.OffsetAngle = NextRandom(random) * 360;
		// Closer pass, closer to the line. The closest one always sits on it, so the straight route is blocked.
		cluster.Offset = flyby.Name == closest ? 0 : MapClamped(flyby.MissLunar, 0, settings.MaxMissLunar, 0, settings.MaxRouteOffset);
		cluster.RockSize = MapClamped(flyby.DiameterMeters, settings.MinRockDiameter, settings.MaxRockDiameter, settings.MinRockSize, settings.MaxRockSize);
		cluster.Drift = MapClamped(flyby.SpeedKmPerSecond, settings.MinFlybySpeed, settings.MaxFlybySpeed, settings.MinDrift, settings.MaxDrift);
		cluster.DiameterMeters = flyby.DiameterMeters;
		cluster.SpeedKmPerSecond = flyby.SpeedKmPerSecond;
		cluster.MissLunar = flyby.MissLunar;
		package.Clusters.push_back(cluster);
	}
	package.AsteroidLabel = sources.FlybysLive ? "LIVE" : "REPLAY " + (flybys.empty() ? package.WindowDate : FormatDate(flybys.back().Time));
	return package;
}

float WindowHour(float matchTime, const DriverSettings& settings)
{
	return settings.MatchLength > 0 ? matchTime / settings.MatchLength * settings.WeatherWindowHours : 0;
}

float SolarWindAt(const MatchPackage& package, float matchTime, const DriverSettings& settings)
{
	const std::vector<TimedValue>& wind = package.SolarWind;
	if (wind.empty())
		return 0;
	const float hour = WindowHour(matchTime, settings);
	auto next = std::lower_bound(wind.begin(), wind.end(), hour, [](const TimedValue& v, float h) { return v.Time < h; });
	if (next == wind.begin())
		return wind.front().Value;
	if (next == wind.end())
		return wind.back().Value;
	const TimedValue& before = *(next - 1);
	const float t = static_cast<float>((hour - before.Time) / (next->Time - before.Time));
	return before.Value + (next->Value - before.Value) * t;
}

float KpAt(const MatchPackage& package, float matchTime, const DriverSettings& settings)
{
	const float hour = WindowHour(matchTime, settings);
	float kp = package.Kp.empty() ? 0 : package.Kp.front().Value;
	for (const TimedValue& value : package.Kp)
	{
		if (value.Time <= hour)
			kp = value.Value;
	}
	return kp;
}

std::string ToText(const MatchPackage& package)
{
	std::string text = "seed\t" + std::to_string(package.Seed) + "\nweather\t" + package.WeatherLabel + "\nasteroids\t" + package.AsteroidLabel
		+ "\ndate\t" + package.WindowDate + "\npeak\t" + Number(package.PeakSolarWind) + "\n";
	for (const TimedValue& v : package.SolarWind)
		text += "wind\t" + Number(v.Time) + "\t" + Number(v.Value) + "\n";
	for (const TimedValue& v : package.Kp)
		text += "kp\t" + Number(v.Time) + "\t" + Number(v.Value) + "\n";
	for (const AsteroidCluster& c : package.Clusters)
		text += "cluster\t" + c.Name + "\t" + Number(c.RouteFraction) + "\t" + Number(c.OffsetAngle) + "\t" + Number(c.Offset) + "\t" + Number(c.RockSize) + "\t"
			+ Number(c.Drift) + "\t" + Number(c.DiameterMeters) + "\t" + Number(c.SpeedKmPerSecond) + "\t" + Number(c.MissLunar) + "\n";
	return text;
}

bool FromText(const std::string& text, MatchPackage& out)
{
	out = {};
	std::stringstream lines(text);
	std::string line;
	auto f = [](const std::string& s) { return std::strtof(s.c_str(), nullptr); };
	while (std::getline(lines, line))
	{
		const std::vector<std::string> p = SplitTabs(line);
		if (p.empty())
			continue;
		const std::string& kind = p[0];
		if (kind == "seed" && p.size() == 2) out.Seed = static_cast<unsigned>(std::strtoul(p[1].c_str(), nullptr, 10));
		else if (kind == "weather" && p.size() == 2) out.WeatherLabel = p[1];
		else if (kind == "asteroids" && p.size() == 2) out.AsteroidLabel = p[1];
		else if (kind == "date" && p.size() == 2) out.WindowDate = p[1];
		else if (kind == "peak" && p.size() == 2) out.PeakSolarWind = f(p[1]);
		else if (kind == "wind" && p.size() == 3) out.SolarWind.push_back({ f(p[1]), f(p[2]) });
		else if (kind == "kp" && p.size() == 3) out.Kp.push_back({ f(p[1]), f(p[2]) });
		else if (kind == "cluster" && p.size() == 10) out.Clusters.push_back({ p[1], f(p[2]), f(p[3]), f(p[4]), f(p[5]), f(p[6]), f(p[7]), f(p[8]), f(p[9]) });
		else
			return false;
	}
	return true;
}
