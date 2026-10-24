#pragma once

#include "DriverSettings.h"

#include "Engine/Engine.h"

#include <string_view>
#include <vector>

class Hud
{
public:
	enum class Align
	{
		Left,
		Center,
		Right,
	};

	void Begin(const FP::TransformComponent& camera, float fieldOfView, const DriverSettings& settings);
	void Text(FP::World& world, FP::Vec2 at, float size, FP::Color color, std::string_view text, Align align = Align::Left, FP::int32 layer = 20, float rollDegrees = 0);
	void Box(FP::World& world, FP::Vec2 center, FP::Vec2 size, FP::Color color, FP::int32 layer = 10);
	void End(FP::World& world);

	float GetAspect() const { return m_Aspect; }

	bool ToScreen(FP::Vec3 point, FP::Vec2& screen) const;

private:
	FP::Entity Take(FP::World& world, std::vector<FP::Entity>& pool, size_t& used, bool glyph);
	void Place(FP::World& world, FP::Entity entity, FP::Vec2 at, FP::Vec2 size, FP::Color color, FP::int32 layer, float rollDegrees);

	std::vector<FP::Entity> m_Glyphs;
	std::vector<FP::Entity> m_Boxes;
	size_t m_GlyphsUsed = 0;
	size_t m_BoxesUsed = 0;

	FP::Vec3 m_Origin;
	FP::Quat m_Rotation;
	float m_Distance = 0.1f;
	float m_Scale = 0.1f;
	float m_Aspect = 1.6f;
};
