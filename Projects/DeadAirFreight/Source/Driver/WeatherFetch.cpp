#include "WeatherFetch.h"

#include "Core/Log.h"
#include "Core/Misc/File.h"
#include "Engine/Paths.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace
{
	std::string FetchOnce(const std::string& url, int timeout)
	{
		char command[1024];
		std::snprintf(command, sizeof(command), "curl -s -f -m %d \"%s\"", timeout, url.c_str());
		std::string body;
		if (FILE* pipe = popen(command, "r"))
		{
			char buffer[65536];
			size_t read = 0;
			while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
				body.append(buffer, read);
			if (pclose(pipe) != 0)
				body.clear();
		}
		return body;
	}

	std::string Fetch(const std::string& url, float timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const auto left = std::chrono::duration_cast<std::chrono::seconds>(deadline - std::chrono::steady_clock::now()).count();
			if (left < 1)
				break;
			std::string body = FetchOnce(url, static_cast<int>(left));
			if (!body.empty())
				return body;
		}
		return {};
	}

	std::string ReadContent(const char* relative)
	{
		FP::String text;
		FP::File::ReadAllText(FP::Paths::Content(relative).CStr(), text);
		return text.CStr();
	}

	std::string Trim(std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
			text.pop_back();
		return text;
	}
}

namespace
{
	// One background gather and its result.
	struct Prefetch
	{
		std::mutex Mutex;
		std::condition_variable Done;
		bool Ready = false;
		MatchSources Sources;
	};

	std::mutex PendingMutex;
	std::shared_ptr<Prefetch> Pending;

	MatchSources GatherNow(const DriverSettings& settings);
}

void PrefetchMatchSources(const DriverSettings& settings)
{
	auto job = std::make_shared<Prefetch>();
	{
		std::lock_guard<std::mutex> lock(PendingMutex);
		Pending = job;
	}
	// Detached, so quitting before the downloads finish never waits on them.
	std::thread([job, settings] {
		MatchSources sources = GatherNow(settings);
		std::lock_guard<std::mutex> lock(job->Mutex);
		job->Sources = std::move(sources);
		job->Ready = true;
		job->Done.notify_all();
	}).detach();
}

MatchSources GatherMatchSources(const DriverSettings& settings)
{
	std::shared_ptr<Prefetch> job;
	{
		std::lock_guard<std::mutex> lock(PendingMutex);
		job = std::move(Pending);
	}
	if (!job)
		return GatherNow(settings);
	std::unique_lock<std::mutex> lock(job->Mutex);
	job->Done.wait(lock, [&] { return job->Ready; });
	return std::move(job->Sources);
}

namespace
{
	MatchSources GatherNow(const DriverSettings& settings)
	{
		MatchSources sources;
		if (settings.FetchLive > 0)
		{
			std::string key = Trim(ReadContent("Settings/NasaApiKey.txt"));
			if (key.empty())
				key = "DEMO_KEY";
			const double now = static_cast<double>(std::time(nullptr));
			const std::string flybyUrl = "https://api.nasa.gov/neo/rest/v1/feed?start_date=" + FormatDate(now - 86400) + "&end_date=" + FormatDate(now) + "&api_key=" + key;

			const float timeout = settings.FetchTimeout;
			auto wind = std::async(std::launch::async, Fetch, "https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json", timeout);
			auto kp = std::async(std::launch::async, Fetch, "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json", timeout);
			auto flybys = std::async(std::launch::async, Fetch, flybyUrl, timeout);

			sources.SolarWind = ParseSolarWind(wind.get());
			sources.Kp = ParseKp(kp.get());
			sources.WeatherLive = !sources.SolarWind.empty() && !sources.Kp.empty();
			sources.Flybys = ParseFlybys(flybys.get());
			sources.FlybysLive = !sources.Flybys.empty();
		}

		if (!sources.WeatherLive)
		{
			FP_LOG("Space weather: live data unavailable, replaying the saved real bundle");
			sources.SolarWind = ParseSolarWind(ReadContent("SpaceWeather/Fallback/SolarWind.json"));
			sources.Kp = ParseKp(ReadContent("SpaceWeather/Fallback/Kp.json"));
		}
		if (!sources.FlybysLive)
		{
			FP_LOG("Asteroids: live flybys unavailable, replaying the saved real bundle");
			sources.Flybys = ParseFlybys(ReadContent("SpaceWeather/Fallback/Flybys.json"));
		}
		return sources;
	}
}
