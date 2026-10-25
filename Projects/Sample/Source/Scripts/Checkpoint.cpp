#include "Engine/Scripting/Script.h"

using namespace FP;

// Saves this entity's position and score. Attach to a player and use the buttons while playing.
FPCLASS(Category = "Gameplay")
class Checkpoint : public Script
{
public:
	FPPROPERTY()
	String Slot = "progress";

	FPPROPERTY()
	int32 Score = 0;

	void OnStart() override { LoadProgress(); }

	// Live values already survive a reload through the reflected fields.
	void OnHotReload() override {}

	FPFUNCTION()
	void SaveProgress()
	{
		SaveData data;
		data.SetInt("score", Score);
		data.SetVec3("position", GetWorldPosition());
		if (!data.Save(Slot.View()))
			FP_LOG_WARN("Could not save checkpoint %s", Slot.CStr());
	}

	FPFUNCTION()
	void LoadProgress()
	{
		SaveData data;
		if (!data.Load(Slot.View()))
			return;
		Score = data.GetInt("score", Score);
		SetWorldPosition(data.GetVec3("position", GetWorldPosition()));
	}
};

FP_SCRIPT(Checkpoint);
