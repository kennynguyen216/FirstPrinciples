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

	// 2D games use X and Y with Rotation.Z as the spin. 3D uses all three axes.
	struct TransformComponent
	{
		Vec3 Position;
		Vec3 Rotation; // euler degrees as pitch X, yaw Y, roll Z
		Vec3 Scale{ 1, 1, 1 };

		Quat GetRotation() const { return Quat::FromEuler(Rotation); }
		Mat4 GetMatrix() const { return Mat4::TRS(Position, GetRotation(), Scale); }

		// Local axes in world space. Forward is -Z, the way cameras and lights point.
		Vec3 GetForward() const { return GetRotation().Rotate({ 0, 0, -1 }); }
		Vec3 GetRight() const { return GetRotation().Rotate({ 1, 0, 0 }); }
		Vec3 GetUp() const { return GetRotation().Rotate({ 0, 1, 0 }); }
	};

	// Flat quad in the XY plane. Size is in world units before Transform.Scale.
	struct SpriteComponent
	{
		Color Tint;
		Vec2 Size{ 1, 1 };
		String Texture; // image relative to Content
		int32 Layer = 0; // higher draws on top
	};

	// Orthographic by default for 2D. Turn on Perspective for 3D.
	struct CameraComponent
	{
		bool Perspective = false;
		float Zoom = 64; // orthographic pixels per world unit
		float FieldOfView = 60; // perspective vertical degrees
		float NearClip = 0.1f;
		float FarClip = 1000;
		bool Primary = true;
		Color Background{ 0.08f, 0.08f, 0.1f, 1 };
	};
}
