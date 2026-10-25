#include "Effects/Starfield.h"

using namespace FP;

void StarfieldSystem::Update(World& world, float)
{
	Entity camera = world.GetActiveCamera();
	if (!world.IsValid(camera))
	{
		for (auto [entity, view] : world.View<CameraComponent>().each())
		{
			if (view.Primary)
				camera = entity;
		}
	}
	if (!world.IsValid(camera))
		return;
	const Vec3 eye = world.GetWorldPosition(camera);
	for (auto [entity, transform, stars] : world.View<TransformComponent, StarfieldComponent>().each())
	{
		transform.Position = eye;
		transform.Scale = { stars.Radius, stars.Radius, stars.Radius };
	}
}
