#include "Engine/Scripting/Script.h"

using namespace FP;

// Counts what lands in a trigger collider on this entity and logs the score.
FPCLASS(Category = "Physics")
class GoalZone : public Script
{
public:
	FPPROPERTY(ReadOnly)
	int32 Score = 0; // things that entered so far

	FPPROPERTY()
	String Counts = "Ball"; // only entities with this name count, empty counts anything

	void OnTriggerEnter(Entity other) override
	{
		if (!Counts.IsEmpty() && GetName(other) != Counts.View())
			return;
		++Score;
		FP_LOG("Goal! %d in %s", Score, GetName(GetEntity()).CStr());
	}
};

FP_SCRIPT(GoalZone);
