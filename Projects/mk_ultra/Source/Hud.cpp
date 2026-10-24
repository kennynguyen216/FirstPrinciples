#include "Hud.h"

#include <cctype>
#include <string>

using namespace FP;

namespace
{
	constexpr float Advance = 0.75f; // glyph cells are 8 font pixels wide, letters step 6

	// Material file name for a character, matching Tools/MakeFont.py. Lowercase draws as uppercase;
	// characters the font doesn't have show as '?'.
	std::string GlyphName(char c)
	{
		const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9'))
			return std::string(1, upper);
		switch (c)
		{
			case '.': return "Period";
			case ',': return "Comma";
			case ':': return "Colon";
			case '%': return "Percent";
			case '/': return "Slash";
			case '-': return "Minus";
			case '+': return "Plus";
			case '(': return "OpenParen";
			case ')': return "CloseParen";
			case '[': return "OpenBracket";
			case ']': return "CloseBracket";
			case '!': return "Exclamation";
			case '>': return "Greater";
			case '<': return "Less";
			case '^': return "Caret";
			case '=': return "Equals";
			case '\'': return "Apostrophe";
			case '"': return "Quote";
			case '#': return "Hash";
			case '*': return "Star";
			case '_': return "Underscore";
			default: return "Question";
		}
	}
}

void Hud::Begin(const TransformComponent& camera, float fieldOfView, const DriverSettings& settings)
{
	m_GlyphsUsed = 0;
	m_BoxesUsed = 0;
	m_Origin = camera.Position;
	m_Rotation = camera.GetRotation();
	m_Distance = settings.HudDistance;
	m_Scale = m_Distance * std::tan(fieldOfView * 0.5f * DegToRad);
	m_Aspect = settings.HudAspect;
}

Entity Hud::Take(World& world, std::vector<Entity>& pool, size_t& used, bool glyph)
{
	if (used == pool.size())
	{
		const Entity entity = world.CreateEntity(glyph ? "HudGlyph" : "HudBox");
		if (glyph)
			world.Add<MaterialComponent>(entity);
		pool.push_back(entity);
	}
	return pool[used++];
}

void Hud::Place(World& world, Entity entity, Vec2 at, Vec2 size, Color color, int32 layer, float rollDegrees)
{
	TransformComponent& transform = world.Get<TransformComponent>(entity);
	transform.Position = m_Origin + m_Rotation.Rotate({ at.X * m_Scale, at.Y * m_Scale, -m_Distance });
	transform.Rotation = (m_Rotation * Quat::FromEuler({ 0, 0, rollDegrees })).ToEuler(); // keeps the camera's own roll
	transform.Scale = { 1, 1, 1 };
	SpriteComponent sprite;
	sprite.Tint = color;
	sprite.Size = size * m_Scale;
	sprite.Layer = layer;
	world.Add<SpriteComponent>(entity, sprite);
}

void Hud::Text(World& world, Vec2 at, float size, Color color, std::string_view text, Align align, int32 layer, float rollDegrees)
{
	const float width = static_cast<float>(text.size()) * size * Advance;
	float x = align == Align::Left ? at.X : align == Align::Center ? at.X - width * 0.5f : at.X - width;
	for (char c : text)
	{
		if (c != ' ')
		{
			const Entity glyph = Take(world, m_Glyphs, m_GlyphsUsed, true);
			const std::string material = "UI/Glyphs/" + GlyphName(c) + ".material";
			MaterialComponent& component = world.Get<MaterialComponent>(glyph);
			if (component.Material != material.c_str())
				component.Material = material.c_str();
			Place(world, glyph, { x + size * 0.5f, at.Y }, { size, size }, color, layer, rollDegrees);
		}
		x += size * Advance;
	}
}

void Hud::Box(World& world, Vec2 center, Vec2 size, Color color, int32 layer)
{
	Place(world, Take(world, m_Boxes, m_BoxesUsed, false), center, size, color, layer, 0);
}

void Hud::End(World& world)
{
	// Unused sprites lose their SpriteComponent so they cost nothing to draw.
	for (size_t i = m_GlyphsUsed; i < m_Glyphs.size(); ++i)
	{
		if (world.IsValid(m_Glyphs[i]) && world.Has<SpriteComponent>(m_Glyphs[i]))
			world.Remove<SpriteComponent>(m_Glyphs[i]);
	}
	for (size_t i = m_BoxesUsed; i < m_Boxes.size(); ++i)
	{
		if (world.IsValid(m_Boxes[i]) && world.Has<SpriteComponent>(m_Boxes[i]))
			world.Remove<SpriteComponent>(m_Boxes[i]);
	}
}

bool Hud::ToScreen(Vec3 point, Vec2& screen) const
{
	const Quat inverse{ -m_Rotation.X, -m_Rotation.Y, -m_Rotation.Z, m_Rotation.W };
	const Vec3 local = inverse.Rotate(point - m_Origin);
	const float halfHeight = m_Scale / m_Distance; // tan of half the vertical field of view
	if (local.Z >= 0)
	{
		screen = Vec2{ local.X, local.Y };
		return false;
	}
	screen = Vec2{ local.X, local.Y } * (1 / (-local.Z * halfHeight));
	return std::abs(screen.X) <= m_Aspect && std::abs(screen.Y) <= 1;
}
