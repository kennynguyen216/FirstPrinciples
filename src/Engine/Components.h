#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

// Built in components. Keep them plain data. Behaviour lives in systems.

namespace FP
{
	// Stable id for saving and referencing entities. Every entity gets one.
	struct IdComponent
	{
		Guid Id;
	};

	struct NameComponent
	{
		String Name;
	};

	struct TransformComponent
	{
		Vec2 Position;
		float Rotation = 0; // degrees, counter clockwise
		Vec2 Scale{ 1, 1 };
	};

	// Draws a colored rectangle, or a .bmp texture tinted by Tint.
	struct SpriteComponent
	{
		Color Tint;
		Vec2 Size{ 1, 1 };
		String Texture; // path relative to Content
		int32 Layer = 0; // higher draws on top
	};

	struct CameraComponent
	{
		float Zoom = 64; // pixels per world unit
		bool Primary = true;
		Color Background{ 0.08f, 0.08f, 0.1f, 1 };
	};
}
