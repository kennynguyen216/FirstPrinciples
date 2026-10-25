#include "Asteroids.h"

#include <algorithm>

using namespace FP;

std::vector<Rock> BuildRocks(const MatchPackage& package, const Route& route, const DriverSettings& settings)
{
	Vec3 right = Vec3::Cross(route.Direction, { 0, 1, 0 });
	right = right.LengthSquared() < 1e-6f ? Vec3{ 1, 0, 0 } : right.Normalized();
	const Vec3 up = Vec3::Cross(right, route.Direction);

	uint32 random = package.Seed * 2654435761u + 1;
	auto signedRandom = [&] { return NextRandom(random) * 2 - 1; };

	std::vector<Rock> rocks;
	for (const AsteroidCluster& cluster : package.Clusters)
	{
		const float angle = cluster.OffsetAngle * DegToRad;
		const Vec3 center = route.Start + route.Direction * (route.Length * cluster.RouteFraction)
			+ (right * std::cos(angle) + up * std::sin(angle)) * cluster.Offset;
		const float spread = cluster.RockSize * settings.ClusterSpread;
		for (int32 i = 0; i < static_cast<int32>(settings.RocksPerCluster); ++i)
		{
			Rock rock;
			rock.Center = center;
			rock.Offset = Vec3{ signedRandom(), signedRandom(), signedRandom() } * spread;
			rock.Axis = Vec3{ signedRandom(), signedRandom(), signedRandom() };
			if (rock.Axis.LengthSquared() < 1e-4f)
				rock.Axis = { 0, 1, 0 };
			rock.Radius = cluster.RockSize * (0.3f + 0.4f * NextRandom(random));
			rock.DegreesPerSecond = cluster.Drift / std::max(rock.Offset.Length(), 1.0f) * RadToDeg;
			rocks.push_back(rock);
		}
	}
	return rocks;
}

Vec3 GetRockPosition(const Rock& rock, float matchTime)
{
	return rock.Center + Quat::AxisAngle(rock.Axis, rock.DegreesPerSecond * matchTime).Rotate(rock.Offset);
}

bool CollideWithRocks(TruckState& truck, const std::vector<Rock>& rocks, float matchTime, const DriverSettings& settings)
{
	for (const Rock& rock : rocks)
	{
		const Vec3 away = truck.Position - GetRockPosition(rock, matchTime);
		const float reach = rock.Radius + settings.TruckRadius;
		if (away.LengthSquared() >= reach * reach)
			continue;
		const Vec3 normal = away.LengthSquared() > 1e-6f ? away.Normalized() : -truck.Travel;
		truck.Position += normal * (reach - away.Length());
		// Only moving into the rock is a hit. Pushed out while already heading away (a drifting rock
		// catching up) is just a nudge, so one collision is one hit.
		if (Vec3::Dot(truck.Travel * truck.Speed + truck.Knockback, normal) >= 0)
			continue;
		// Knocked straight back, still facing the same way, so the view doesn't swing.
		truck.Knockback = normal * (settings.BounceDistance * settings.BounceRecoil);
		truck.Speed *= settings.BounceSpeedKeep;
		return true;
	}
	return false;
}
