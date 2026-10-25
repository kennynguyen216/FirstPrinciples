#include "Engine/Scripting/Script.h"

#include <algorithm>
#include <cmath>

using namespace FP;

// A long constant-speed pass. P compares Doppler, F freezes motion, R restarts at the left end.
class DopplerFlyby : public Script
{
public:
	FPPROPERTY(Range = "10, 1000")
	float HalfDistance = 250; // half the total route length, in meters
	FPPROPERTY(Range = "1, 150")
	float Speed = 45; // meters per second
	FPPROPERTY()
	Vec3 Center{0, 4, 0};

	void OnStart() override
	{
		Restart();
		FP_LOG("Long Doppler flyby: P Doppler on/off, F freeze motion, R restart. WASD walk, Shift run, mouse look.");
	}

	void OnUpdate(float deltaTime) override
	{
		auto* source = TryGet<AudioSourceComponent>();
		if (!source) return;
		if (Input::WasKeyPressed(SDL_SCANCODE_P))
		{
			source->DopplerScale = source->DopplerScale > 0 ? 0.f : 1.f;
			FP_LOG("Doppler: %s", source->DopplerScale > 0 ? "ON" : "OFF");
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_F)) m_Paused = !m_Paused;
		if (Input::WasKeyPressed(SDL_SCANCODE_R)) { Restart(); return; }
		if (m_Paused || !Valid() || !std::isfinite(deltaTime) || deltaTime <= 0) return;
		m_Travel = std::fmod(m_Travel + Speed * std::min(deltaTime, .05f), 4 * HalfDistance);
		const float offset = m_Travel <= 2 * HalfDistance ? m_Travel - HalfDistance : 3 * HalfDistance - m_Travel;
		GetTransform().Position = Center + Vec3{offset, 0, 0};
	}

private:
	bool Valid() const
	{
		return std::isfinite(HalfDistance) && HalfDistance >= 10 && HalfDistance <= 1000
			&& std::isfinite(Speed) && Speed >= 1 && Speed <= 150
			&& std::isfinite(Center.X) && std::isfinite(Center.Y) && std::isfinite(Center.Z);
	}
	void Restart()
	{
		if (!Valid()) return;
		m_Travel = 0; m_Paused = false;
		GetTransform().Position = Center - Vec3{HalfDistance, 0, 0};
		if (auto* source = TryGet<AudioSourceComponent>())
		{
			source->Enabled = true;
			const auto played = GetWorld().GetAudio().Play(GetEntity());
			if (!played) FP_LOG_WARN("Doppler flyby: %s", played.Result.Message.CStr());
		}
	}
	float m_Travel = 0;
	bool m_Paused = false;
};

FP_SCRIPT(DopplerFlyby);
