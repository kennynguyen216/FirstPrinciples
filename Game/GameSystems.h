#pragma once

#include "Game/GameComponents.h"

class PlayerSystem : public FP::System
{
public:
	const char* GetName() const override { return "Player"; }
	void Update(FP::World& world, float deltaTime) override;
};

class SpinnerSystem : public FP::System
{
public:
	const char* GetName() const override { return "Spinner"; }
	void Update(FP::World& world, float deltaTime) override;
};

class CameraFollowSystem : public FP::System
{
public:
	const char* GetName() const override { return "CameraFollow"; }
	void Update(FP::World& world, float deltaTime) override;
};
