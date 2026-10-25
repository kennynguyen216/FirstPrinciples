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
		String Clip; // WAV or MP3 relative to Content
		bool Enabled = true;
		float Volume = 1;
		float Pitch = 1;
		bool Loop = false;
		bool PlayOnStart = false;
		String Bus = "SFX";
		bool Spatial = false;
		bool Occlusion = true;
		float DopplerScale = 1;
		float MinDistance = 1;
		float MaxDistance = 25;
		int32 Priority = 128;
	};

	struct AudioListenerComponent
	{
		bool Enabled = true;
	};

	struct AudioRoomComponent
	{
		bool Enabled = true;
		Vec3 Size{ 8, 3, 8 }; // local dimensions of the room interior, in meters
		float Wet = 0.5f;
		float Decay = 1; // seconds to decay by 60 dB
	};

	struct VoiceChatComponent
	{
		bool Enabled = true;
		float Volume = 1;
		float MinDistance = 1;
		float MaxDistance = 25;
		bool Occlusion = true;
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

	// A capsule that walks: it slides along walls, climbs steps and slopes, rides moving platforms
	// and pushes things. Scripts move it with Move and Jump. Use it instead of a collider and rigid
	// body. Its shape sits centered on the entity and always stands upright.
	struct CharacterControllerComponent
	{
		float Radius = 0.5f;
		float Height = 2;          // end to end, standing along Y
		float StepHeight = 0.35f;  // steps up to this tall are climbed
		float MaxSlope = 50;       // steepest walkable ground in degrees
		float Mass = 70;           // kilograms, for pushing things and weighing on them
		float AirControl = 0.1f;   // how much Move steers in the air, 0 to 1
		float GravityScale = 1;
	};

	// Plays a character controller with no script, from the Move, Look, Run and Jump input
	// actions, so the keyboard and mouse or a gamepad both work. A child camera named Camera
	// tilts as it looks up and down.
	struct PlayerInputComponent
	{
		float WalkSpeed = 5;
		float RunSpeed = 9;   // while Shift is held
		float JumpSpeed = 6;
		float LookSpeed = 0.15f; // degrees per pixel of mouse movement
		String Camera = "Player Camera";
	};

	// Light at the transform's position. In 2D, Position.Z is the height above the sprites.
	struct PointLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 1;
		float Range = 6;
		bool CastShadows = true;
		int32 ShadowResolution = 512;
		float ShadowStrength = 1;
	};

	// Cone of light along the transform's forward (-Z) axis.
	struct SpotLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 };
		float Intensity = 1;
		float Range = 10;
		float InnerAngle = 25; // degrees from the cone axis
		float OuterAngle = 35;
		bool CastShadows = true;
		int32 ShadowResolution = 1024;
		float ShadowStrength = 1;
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

	// One scene-wide settings entity for visual effects.
	struct PostProcessComponent
	{
		bool FXAA = false;
		bool Bloom = false;
		float BloomIntensity = 0.15f;
		float BloomThreshold = 1;
		bool WideBloom = false; // eight octaves of glow out to 1/256 of the screen instead of one short blur
	};

	// Flat light added everywhere so unlit sides are not pure black.
	struct AmbientLightComponent
	{
		::FP::Color Color{ 1, 1, 1, 1 }; // qualified so the member can share the type name
		float Intensity = 0.15f;
	};

	// A panorama supplies the sky, diffuse lighting and reflections.
	struct SkyboxComponent
	{
		String Texture;
		float Intensity = 1;
		float Rotation = 0; // degrees around Y
		bool Visible = true;
		bool Enabled = true;
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

	// Sprays small camera facing quads, for fire, smoke, sparks, dust and magic. Particles fly on
	// their own once emitted. Scripts call EmitParticles for one off bursts.
	struct ParticleEmitterComponent
	{
		bool Emitting = true;
		float Rate = 20;             // particles per second
		int32 Burst = 0;             // particles all at once when play starts
		int32 MaxParticles = 500;
		float Lifetime = 1.5f;       // seconds
		float LifetimeRandom = 0.3f; // up to this fraction shorter
		String Shape = "Point";      // Point, Sphere or Box
		Vec3 ShapeSize{ 0.5f, 0.5f, 0.5f }; // Sphere radius is X, Box is full size
		Vec3 Direction{ 0, 1, 0 };   // in the entity's space
		float Spread = 20;           // cone half angle in degrees, 180 sprays every way
		float Speed = 2;
		float SpeedRandom = 0.5f;    // up to this fraction slower
		float Gravity = 0;           // 1 falls like everything else, negative rises
		float Drag = 0;              // how fast they slow down
		float StartSize = 0.25f;
		float EndSize = 0.05f;
		Color StartColor{ 1, 0.8f, 0.4f, 1 };
		Color EndColor{ 1, 0.2f, 0.05f, 0 };
		String Texture;              // a soft dot when empty
		String Blend = "Additive";   // Additive glows, Alpha covers
		bool WorldSpace = true;      // off makes particles move with the entity
		bool DestroyWhenDone = false; // removes the entity once it stops emitting and its particles are gone

		int32 PendingBurst = 0;      // queued by EmitParticles, not saved
	};
}
