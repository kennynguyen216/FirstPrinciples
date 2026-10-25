#pragma once

#include "SpaceWeather.h"

MatchSources GatherMatchSources(const DriverSettings& settings);

// Starts gathering in the background, so the downloads are done by the time the match starts
// instead of the game freezing on them. The next GatherMatchSources takes the result, waiting only
// for whatever is still on its way.
void PrefetchMatchSources(const DriverSettings& settings);
