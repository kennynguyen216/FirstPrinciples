#pragma once

#include "Engine/World.h"

namespace FP
{
	struct Ray
	{
		Vec3 Origin;
		Vec3 Direction;

		// Where the ray meets the plane z = planeZ. False when parallel or behind.
		bool IntersectZPlane(float planeZ, Vec3& outPoint) const;
	};

	// How surfaces are shaded. Editors switch this to inspect a scene.
	enum class ViewMode : uint8
	{
		Lit,       // full materials and lights
		Unlit,     // base color times tint, no lights
		Wireframe, // triangle edges only
	};

	// Everything the renderer needs to know about the camera for one frame.
	struct RenderView
	{
		Mat4 View;
		Mat4 Projection;
		Vec3 CameraPosition;
		Vec3 CameraForward{ 0, 0, -1 };
		Vec3 CameraRight{ 1, 0, 0 };
		Vec3 CameraUp{ 0, 1, 0 };
		Color Background{ 0.08f, 0.08f, 0.1f, 1 };
		bool Orthographic = true;
		float PixelsPerUnit = 64; // orthographic only
		float Exposure = 1;
		bool Tonemap = true;
		ViewMode Mode = ViewMode::Lit;
		uint32 Width = 1;
		uint32 Height = 1;

		Mat4 GetViewProjection() const { return Projection * View; }

		// Pixel coordinates from the top left of the target.
		Ray ScreenToRay(Vec2 pixel) const;
		Vec2 WorldToScreen(Vec3 world) const;

		// Size of one pixel in world units at a point. Keeps editor gizmos a constant screen size.
		float WorldPerPixel(Vec3 world) const;

		// The world's primary camera, or a default 2D view when there is none.
		static RenderView FromWorld(World& world, uint32 width, uint32 height);

		static RenderView FromCamera(const TransformComponent& transform, const CameraComponent& camera, uint32 width, uint32 height);

		// Top down 2D view centred on center, as used by the editor.
		static RenderView Orthographic2D(Vec2 center, float pixelsPerUnit, uint32 width, uint32 height);
	};
}
