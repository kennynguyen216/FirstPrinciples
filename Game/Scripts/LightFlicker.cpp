#include "Engine/Scripting/Script.h"

#include <cmath>

using namespace FP;

// Makes a point light flicker like a candle. Needs a PointLight on the same entity.
class LightFlicker : public Script
{
public:
	float Amount = 0.35f;
	float Speed = 9;

	void OnStart() override
	{
		if (PointLightComponent* light = TryGet<PointLightComponent>())
			m_BaseIntensity = light->Intensity;
		else
			FP_LOG_WARN("LightFlicker on %s needs a PointLight", GetWorld().GetEntityName(GetEntity()).CStr());
	}

	void OnUpdate(float deltaTime) override
	{
		m_Time += deltaTime * Speed;
		if (PointLightComponent* light = TryGet<PointLightComponent>())
		{
			// Two sine waves at odd ratios look less regular than one.
			const float wave = std::sin(m_Time) * 0.6f + std::sin(m_Time * 2.71f) * 0.4f;
			light->Intensity = m_BaseIntensity * (1.0f + wave * Amount);
		}
	}

	void OnStop() override
	{
		if (PointLightComponent* light = TryGet<PointLightComponent>())
			light->Intensity = m_BaseIntensity;
	}

private:
	float m_BaseIntensity = 1;
	float m_Time = 0;
};

FP_SCRIPT(LightFlicker)
{
	fields.Field("Amount", &LightFlicker::Amount);
	fields.Field("Speed", &LightFlicker::Speed);
}
