#include "BlackHoles.h"

#include <algorithm>

using namespace FP;

std::vector<BlackHole> BuildBlackHoles(const MatchPackage& package, const Route& route, const DriverSettings& settings)
{
	Vec3 right = Vec3::Cross(route.Direction, { 0, 1, 0 });
	right = right.LengthSquared() < 1e-6f ? Vec3{ 1, 0, 0 } : right.Normalized();
	const Vec3 up = Vec3::Cross(right, route.Direction);

	uint32 random = package.Seed * 40503u + 7;
	std::vector<BlackHole> holes;
	const int32 count = static_cast<int32>(settings.BlackHoleCount);
	for (int32 i = 0; i < count; ++i)
	{
		// Spread evenly over the same stretch as the asteroids, each a little off the route line.
		const float fraction = settings.RouteClusterStart + (settings.RouteClusterEnd - settings.RouteClusterStart) * (i + 0.5f) / count;
		const float angle = NextRandom(random) * 2 * Pi;
		BlackHole hole;
		hole.CoreRadius = settings.BlackHoleCoreRadius;
		hole.RingRadius = settings.BlackHoleRingRadius;
		hole.Position = route.Start + route.Direction * (route.Length * fraction)
			+ (right * std::cos(angle) + up * std::sin(angle)) * (hole.RingRadius * settings.BlackHoleRouteOffset);
		holes.push_back(hole);
	}
	return holes;
}

BlackHoleEvent StepBlackHoles(TruckState& truck, std::vector<BlackHole>& holes, const DriverSettings& settings, float deltaTime)
{
	BlackHoleEvent event = BlackHoleEvent::None;
	for (BlackHole& hole : holes)
	{
		const Vec3 toHole = hole.Position - truck.Position;
		const float distance = toHole.Length();
		const float core = hole.CoreRadius + settings.TruckRadius;
		if (distance >= hole.RingRadius)
		{
			hole.Charge = 0; // each pass charges on its own
			continue;
		}
		if (distance < core)
		{
			// Too deep: spat back out of the core, slowed to a crawl, and the charge is lost.
			const Vec3 normal = distance > 1e-4f ? toHole * (-1 / distance) : -truck.Travel;
			truck.Position = hole.Position + normal * core;
			truck.Travel = normal;
			truck.Speed *= settings.BounceSpeedKeep;
			hole.Charge = 0;
			event = BlackHoleEvent::Swallowed;
			continue;
		}
		// Pull gets stronger towards the core. The nose keeps winning it back, so it feels like a drag.
		const float closeness = 1 - (distance - core) / std::max(hole.RingRadius - core, 1e-3f);
		truck.Travel = RotateTowards(truck.Travel, toHole * (1 / distance), settings.BlackHolePull * closeness * deltaTime);
		if (hole.Spent)
			continue;
		// Charges faster the nearer the core, so cutting straight through the ring isn't enough.
		hole.Charge += closeness * deltaTime;
		if (hole.Charge >= settings.SlingshotTime)
		{
			hole.Spent = true;
			truck.Boost = settings.BoostTime;
			event = BlackHoleEvent::Slingshot;
		}
	}
	return event;
}
