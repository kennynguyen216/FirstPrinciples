#include "Game.h"
#include "DriverSystem.h"

using namespace FP;

namespace Game
{
	void RegisterComponents()
	{
	}

	void AddSystems(World& world)
	{
		world.AddSystem<DriverSystem>();
	}
}
