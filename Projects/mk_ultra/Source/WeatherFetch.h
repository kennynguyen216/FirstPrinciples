#pragma once

#include "SpaceWeather.h"

// Gathers the real data one match is built from: live from NOAA and NASA when FetchLive is on and the
// requests work, otherwise the saved fallback bundle in Content/SpaceWeather/Fallback.
// The solar wind and Kp fall back together so they always cover the same 24 hours.
// Blocks for at most about FetchTimeout seconds, once, at match start.
MatchSources GatherMatchSources(const DriverSettings& settings);
