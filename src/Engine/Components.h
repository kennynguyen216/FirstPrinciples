#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Math.h"

#include <entt/entity/entity.hpp>

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

	// Marks the one entity per scene that holds scene wide scripts, like game rules or level
	// logic. The editor shows it as the scene row at the top of the Hierarchy.
	struct SceneComponent
	{
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

	// Makes the entity a child of another. Its Transform is then relative to the parent, so it
	// moves, turns and scales with it. Set it with World::SetParent rather than by hand.
	struct ParentComponent
	{
		Guid Parent;
		entt::entity Cached = entt::null; // found from Parent on first use
	};

	// 2D games use X and Y with Rotation.Z as the spin. 3D uses all three axes.
	// Relative to the parent when the entity has one, otherwise in world space.
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

	// A built in shape (Cube, Sphere, Plane, Quad), or an .obj, .gltf or .glb file relative to Content.
	// Uses the entity's MaterialComponent for shading, one material per mesh section.
	struct MeshComponent
	{
		String Mesh = "Cube";
	};

	// A .gltf or .glb with a skin, relative to Content. Draws in its bind pose until an
	// Animator moves its bones. Uses the entity's MaterialComponent like MeshComponent.
	struct SkeletalMeshComponent
	{
		String Mesh;
	};

	// Plays animation clips on the entity's SkeletalMeshComponent while the game runs.
	// Loops Clip unless a script builds a state machine with GetAnimator().
	struct AnimatorComponent
	{
		String Clip;
		float Speed = 1;
		bool Playing = true;
	};

	// Bone matrices for a skeletal mesh this frame, written by the AnimationSystem.
	// Not saved and not shown in the editor.
	struct SkeletalPoseComponent
	{
		Array<Mat4> Skin;
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

	// The shape physics sees for an entity. Alone it is a solid, unmoving part of the level.
	// Add a RigidBodyComponent to make it move. Sizes scale with the transform.
	struct ColliderComponent
	{
		String Shape = "Box"; // Box, Sphere, Capsule or Mesh (the entity's mesh)
		Vec3 Size{ 1, 1, 1 }; // Box, full size before scaling
		float Radius = 0.5f;  // Sphere and Capsule
		float Height = 2;     // Capsule, end to end, standing along Y
		Vec3 Offset;          // from the entity's origin
		bool Trigger = false; // reports overlaps instead of blocking
	};

	// Lets physics move the entity. Dynamic bodies fall and collide, kinematic ones follow their
	// transform and push dynamic ones aside, and static ones never move.
	struct RigidBodyComponent
	{
		String Motion = "Dynamic"; // Dynamic, Kinematic or Static
		float Mass = 1; // kilograms
		float Friction = 0.5f;
		float Restitution = 0; // bounciness, 0 to 1
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityScale = 1;
		bool LockRotation = false; // keeps it upright, for characters
		bool Continuous = false;   // stops fast small bodies passing through thin walls
	};

	// Light at the transform's position. In 2D, Position.Z is the height above the sprites.
	struct PointLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 1;
		float Range = 6;
	};

	// Light from far away, shining along the transform's forward (-Z) axis.
	// The first one with CastShadows casts shadows from meshes within ShadowDistance of the camera.
	struct DirectionalLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 1;
		bool CastShadows = true;
		int32 ShadowResolution = 1024; // pixels per side. Lower is faster and softer.
		float ShadowDistance = 20; // world units covered, centered ahead of the camera
		float ShadowStrength = 1; // 0 is no shadow, 1 is only ambient light in shadow
	};

	// Flat light added everywhere so unlit sides are not pure black.
	struct AmbientLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 0.15f;
	};

	// An HTML page from Content drawn over the screen, written with HTML, CSS and JavaScript.
	// On a camera it draws over that camera's view, like a Unity canvas. On any other entity it
	// draws over every view. The page talks to the entity's scripts through the engine object.
	struct UIComponent
	{
		String Page; // like "UI/HUD.html"
		bool Visible = true;
		bool Input = true; // gets the mouse and keyboard
		int32 Order = 0;   // higher draws on top
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
