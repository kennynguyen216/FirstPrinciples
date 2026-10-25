#pragma once

#include "GameComponents.h"

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

class FollowPlayerSystem : public FP::System
{
public:
	const char* GetName() const override { return "FollowPlayer"; }
	void Update(FP::World& world, float deltaTime) override;
};

class AudioDemoSystem : public FP::System
{
public:
	const char* GetName() const override { return "AudioDemo"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
};

class AcousticsTestSystem : public FP::System
{
public:
	const char* GetName() const override { return "AcousticsTest"; }
	void OnStart(FP::World& world) override;
	void Update(FP::World& world, float deltaTime) override;
};
