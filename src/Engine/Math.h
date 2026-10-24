#pragma once

#include "HAL/Platform.h"

#include <cmath>

namespace FP
{
	constexpr float Pi = 3.14159265358979f;
	constexpr float DegToRad = Pi / 180.0f;

	struct Vec2
	{
		float X = 0;
		float Y = 0;

		constexpr Vec2() = default;
		constexpr Vec2(float x, float y) : X(x), Y(y) {}

		constexpr Vec2 operator+(Vec2 o) const { return { X + o.X, Y + o.Y }; }
		constexpr Vec2 operator-(Vec2 o) const { return { X - o.X, Y - o.Y }; }
		constexpr Vec2 operator*(float s) const { return { X * s, Y * s }; }
		constexpr Vec2 operator*(Vec2 o) const { return { X * o.X, Y * o.Y }; }
		constexpr Vec2 operator/(float s) const { return { X / s, Y / s }; }
		constexpr Vec2 operator-() const { return { -X, -Y }; }
		Vec2& operator+=(Vec2 o) { X += o.X; Y += o.Y; return *this; }
		Vec2& operator-=(Vec2 o) { X -= o.X; Y -= o.Y; return *this; }
		Vec2& operator*=(float s) { X *= s; Y *= s; return *this; }
		constexpr bool operator==(const Vec2&) const = default;

		float Length() const { return std::sqrt(X * X + Y * Y); }
		constexpr float LengthSquared() const { return X * X + Y * Y; }

		Vec2 Normalized() const
		{
			const float len = Length();
			return len > 1e-6f ? *this / len : Vec2();
		}

		// Rotates counter clockwise by degrees.
		Vec2 Rotated(float degrees) const
		{
			const float r = degrees * DegToRad;
			const float c = std::cos(r);
			const float s = std::sin(r);
			return { X * c - Y * s, X * s + Y * c };
		}
	};

	// Linear 0 to 1 color.
	struct Color
	{
		float R = 1;
		float G = 1;
		float B = 1;
		float A = 1;

		constexpr bool operator==(const Color&) const = default;

		static constexpr Color White() { return { 1, 1, 1, 1 }; }
		static constexpr Color Black() { return { 0, 0, 0, 1 }; }
	};
}
