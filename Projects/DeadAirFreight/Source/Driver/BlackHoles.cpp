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
		// The first one comes up soon after the start.
		const float along = i == 0 ? std::min(settings.FirstBlackHoleDistance, route.Length * fraction) : route.Length * fraction;
		hole.Position = route.Start + route.Direction * along
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
			SetTravel(truck, normal, settings);
			truck.Speed *= settings.BounceSpeedKeep;
			hole.Charge = 0;
			event = BlackHoleEvent::Swallowed;
			continue;
		}
		// Pull gets stronger towards the core. The nose keeps winning it back, so it feels like a drag.
		const float closeness = 1 - (distance - core) / std::max(hole.RingRadius - core, 1e-3f);
		// With the slow helm the pull drags the look, like turbulence, and the ship comes about to follow it.
		const float pull = settings.BlackHolePull * closeness * deltaTime;
		if (settings.NoseTurnRate > 0)
		{
			const Vec3 look = Quat::FromEuler({ truck.Look.Y, truck.Look.X, 0 }).Rotate({ 0, 0, -1 });
			const Vec2 bent = ToYawPitch(RotateTowards(look, toHole * (1 / distance), pull));
			truck.Look = { truck.Look.X + std::remainder(bent.X - truck.Look.X, 360.0f), bent.Y };
		}
		else
		{
			truck.Travel = RotateTowards(truck.Travel, toHole * (1 / distance), pull);
		}
		if (hole.Spent)
			continue;
		// Charges anywhere inside the ring, out to its edge.
		hole.Charge += deltaTime;
		if (hole.Charge >= settings.SlingshotTime)
		{
			hole.Spent = true;
			truck.Boost = settings.BoostTime;
			event = BlackHoleEvent::Slingshot;
		}
	}
	return event;
}
