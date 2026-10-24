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

	struct AudioSourceComponent
	{
		String Clip; // WAV relative to Content
		bool Enabled = true;
		float Volume = 1;
		float Pitch = 1;
		bool Loop = false;
		bool PlayOnStart = false;
		String Bus = "SFX";
		bool Spatial = false;
		float MinDistance = 1;
		float MaxDistance = 25;
		int32 Priority = 128;
	};

	struct AudioListenerComponent
	{
		bool Enabled = true;
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

	// A built in shape (Cube, Sphere, Plane, Quad) or a .obj file relative to Content.
	// Uses the entity's MaterialComponent for shading, one material per mesh section.
	struct MeshComponent
	{
		String Mesh = "Cube";
	};

	// Points at shared .material files. Works on sprites and meshes.
	// A sprite's own Texture is used as the base color map when the material has none.
	struct MaterialComponent
	{
		String Material; // used by every section without its own slot
		Array<String> Slots; // one per mesh section, empty means use Material

		// The material for one mesh section.
		const String& GetForSection(int32 section) const
		{
			return Slots.IsValidIndex(section) && !Slots[section].IsEmpty() ? Slots[section] : Material;
		}
	};

	// Light at the transform's position. In 2D, Position.Z is the height above the sprites.
	struct PointLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 1;
		float Range = 6;
	};

	// Light from far away, shining along the transform's forward (-Z) axis.
	struct DirectionalLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 1;
	};

	// Flat light added everywhere so unlit sides are not pure black.
	struct AmbientLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 0.15f;
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
		float Exposure = 1;
		bool Tonemap = true; // soft roll off for lights brighter than white
	};
}
