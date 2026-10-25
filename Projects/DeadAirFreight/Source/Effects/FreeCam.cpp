#include "Effects/FreeCam.h"
#include "Crew/CrewComponents.h"
#include "Driver/Cab.h"
#include "Driver/Truck.h"
#include "Effects/Starfield.h"
#include "Match/MatchQueries.h"
#include "Match/MatchSystems.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace FP;

namespace
{
	// Tuned by eye for smooth trailer moves. Higher smoothing follows the controls more tightly.
	constexpr float MoveSpeed = 8;
	constexpr float FastScale = 6;
	constexpr float SlowScale = 0.2f;
	constexpr float MoveSmoothing = 4;
	constexpr float LookSensitivity = 0.1f; // degrees per pixel
	constexpr float LookSmoothing = 8;
	constexpr float ZoomPerNotch = 3; // degrees of field of view
	constexpr float ZoomSmoothing = 8;
	// The SpaceTruck model's bounds before the Truck entity's scale, from Meshes/SpaceTruck.
	const Vec3 HullMin{ -1.21f, -1.42f, -2.6f };
	const Vec3 HullMax{ 1.21f, 1.55f, 7.74f };
	// The engine refuses entities scaled to zero.
	const Vec3 HiddenScale{ 0.001f, 0.001f, 0.001f };

	// One camera shared by both systems.
	struct
	{
		bool On = false;
		bool Flying = true;
		bool InSpace = false; // held still in space rather than riding along with the player
		Vec3 Offset;          // from the anchor while riding, or the world position while InSpace
		Vec3 Velocity;
		Vec2 Look, LookTarget; // pitch, yaw in degrees
		float Fov = 60, FovTarget = 60, GameFov = 60;
		Vec3 Anchor, Written; // where the game put the camera, and where we last put it
		double Stepped = -1;  // the world time the controls were last read
		std::vector<Entity> Hidden; // pages we hid
		// Inside parts hidden from outside, with the scale and light they had.
		struct Stowed
		{
			Vec3 Scale;
			float Light = 0;
		};
		std::unordered_map<Entity, Stowed> Shrunk;
		MeshComponent TruckMesh;     // the model, kept from before DriverSystem takes it off the truck
		MaterialComponent TruckLook;
		bool HasTruckMesh = false;
		bool ShellShown = false;
		Entity Self = NullEntity; // this player's character while they are in the cab
	} s_Cam;

	void SetHud(World& world, bool show)
	{
		if (show)
		{
			for (Entity entity : s_Cam.Hidden)
				if (UIComponent* page = world.IsValid(entity) ? world.TryGet<UIComponent>(entity) : nullptr)
					page->Visible = true;
			s_Cam.Hidden.clear();
			return;
		}
		for (auto [entity, page] : world.View<UIComponent>().each())
		{
			if (page.Visible)
			{
				page.Visible = false;
				s_Cam.Hidden.push_back(entity);
			}
		}
	}

	Vec3 Position() { return s_Cam.InSpace ? s_Cam.Offset : s_Cam.Anchor + s_Cam.Offset; }

	Quat Inverse(Quat q) { return { -q.X, -q.Y, -q.Z, q.W }; }

	// Inside the truck's hull, or in one of the rooms far from the race (the bay).
	bool IsInside(World& world, Vec3 eye, const TransformComponent& truck)
	{
		const Vec3 local = Inverse(truck.GetRotation()).Rotate(eye - truck.Position);
		const Vec3 model{ local.X / truck.Scale.X, local.Y / truck.Scale.Y, local.Z / truck.Scale.Z };
		if (model.X >= HullMin.X && model.X <= HullMax.X && model.Y >= HullMin.Y && model.Y <= HullMax.Y && model.Z >= HullMin.Z && model.Z <= HullMax.Z)
			return true;
		for (auto [entity, transform, room] : world.View<TransformComponent, InteriorComponent>().each())
		{
			const Vec3 at = eye - transform.Position;
			if (std::abs(at.X) <= room.HalfWidth + 1 && std::abs(at.Z) <= room.HalfLength + 1 && at.Y >= -1 && at.Y <= 8)
				return true;
		}
		return false;
	}

	// What rides inside the truck: the Driver's cab parts (named the way DriverSystem finds them),
	// crewmates standing in it, and TransitSystem's ghost of the bay behind the hatch.
	bool IsInsidePart(std::string_view name)
	{
		return name.starts_with("Cab") || name.starts_with("Canister") || name == "Wheel" || name == "Scrubber"
			|| name == "Ghost" || name == "Crewmate In Cab";
	}

	void ShowOutside(World& world, Entity truck, bool outside)
	{
		if (outside)
		{
			for (auto [entity, name, transform] : world.View<NameComponent, TransformComponent>().each())
			{
				if (!IsInsidePart(name.Name.CStr()) || !(world.Has<MeshComponent>(entity) || world.Has<PointLightComponent>(entity)))
					continue;
				// Ghosts, crewmates and the cab light are set again every frame, so they are hidden
				// every frame. Point lights ignore scale, so lights are dimmed instead.
				PointLightComponent* light = world.TryGet<PointLightComponent>(entity);
				if (!s_Cam.Shrunk.contains(entity) || !(transform.Scale == HiddenScale) || (light && light->Intensity > 0))
					s_Cam.Shrunk[entity] = { transform.Scale, light ? light->Intensity : 0 };
				transform.Scale = HiddenScale;
				if (light)
					light->Intensity = 0;
			}
		}
		else
		{
			for (auto [entity, stowed] : s_Cam.Shrunk)
			{
				if (!world.IsValid(entity))
					continue;
				world.Get<TransformComponent>(entity).Scale = stowed.Scale;
				if (PointLightComponent* light = world.TryGet<PointLightComponent>(entity))
					light->Intensity = stowed.Light;
			}
			s_Cam.Shrunk.clear();
		}

		if (outside && !s_Cam.ShellShown && s_Cam.HasTruckMesh)
		{
			world.Add<MeshComponent>(truck, s_Cam.TruckMesh);
			world.Add<MaterialComponent>(truck, s_Cam.TruckLook);
		}
		else if (!outside && s_Cam.ShellShown)
		{
			world.Remove<MeshComponent>(truck);
		}
		s_Cam.ShellShown = outside && s_Cam.HasTruckMesh;
	}

	// This player's character standing in the cab, which only other players' copies of it ever had.
	void PlaceSelf(World& world, const TransformComponent& truck, bool show)
	{
		const Entity body = world.FindEntityByName("Driver");
		const DriverState* driver = world.IsValid(body) ? world.TryGet<DriverState>(body) : nullptr;
		const Entity member = MatchQueries::FindLocalMember(world);
		if (!show || !driver || !driver->InCab || member == NullEntity)
		{
			if (world.IsValid(s_Cam.Self))
				world.DestroyEntity(s_Cam.Self);
			s_Cam.Self = NullEntity;
			return;
		}
		if (!world.IsValid(s_Cam.Self))
		{
			s_Cam.Self = world.CreateEntity("FreeCam Self");
			DressCrewCharacter(world, s_Cam.Self, world.Get<CrewMemberComponent>(member).Crew);
		}
		TransformComponent& transform = world.Get<TransformComponent>(s_Cam.Self);
		transform.Position = world.Get<TransformComponent>(body).Position;
		transform.Rotation = (truck.GetRotation() * Quat::FromEuler({ 0, driver->Yaw + 90, 0 })).ToEuler();
	}

	void ShowInsideAndOut(World& world, Vec3 eye)
	{
		const Entity truck = world.FindEntityByName("Truck");
		if (!world.IsValid(truck))
			return;
		// Kept while the scene's model is still on it, before GO.
		if (!s_Cam.ShellShown && world.Has<MeshComponent>(truck))
		{
			s_Cam.TruckMesh = world.Get<MeshComponent>(truck);
			if (const MaterialComponent* look = world.TryGet<MaterialComponent>(truck))
				s_Cam.TruckLook = *look;
			s_Cam.HasTruckMesh = true;
		}
		// Until the Driver's systems start, the cab is still off in its room, not riding the truck.
		const bool riding = world.Has<TruckState>(truck);
		const TransformComponent& transform = world.Get<TransformComponent>(truck);
		const bool outside = s_Cam.On && riding && !IsInside(world, eye, transform);
		if (outside || s_Cam.ShellShown || !s_Cam.Shrunk.empty())
			ShowOutside(world, truck, outside);
		PlaceSelf(world, transform, s_Cam.On && riding && !outside);
	}
}

bool FreeCam::IsFlying()
{
	return s_Cam.On && s_Cam.Flying;
}

bool FreeCam::IsOn()
{
	return s_Cam.On;
}

void FreeCamSystem::Update(World& world, float deltaTime)
{
	const MatchStateComponent* state = MatchQueries::GetState(world);
	const Entity camera = world.GetActiveCamera();
	if (!state || state->Phase == MatchPhase::Title || !world.IsValid(camera))
		return;
	TransformComponent& transform = world.Get<TransformComponent>(camera);
	CameraComponent& view = world.Get<CameraComponent>(camera);

	// Whatever moved the camera since our last write is the game's view, which riding follows.
	if (!(transform.Position == s_Cam.Written))
		s_Cam.Anchor = transform.Position;

	// The controls are read once a frame, by whichever of the two systems runs first.
	const bool firstThisFrame = world.GetTime() != s_Cam.Stepped;
	s_Cam.Stepped = world.GetTime();
	if (firstThisFrame && Input::WasKeyPressed(SDL_SCANCODE_F))
	{
		s_Cam.On = !s_Cam.On;
		SetHud(world, !s_Cam.On);
		if (!s_Cam.On)
		{
			view.FieldOfView = s_Cam.GameFov;
			ShowInsideAndOut(world, transform.Position);
			return;
		}
		s_Cam.Flying = true;
		s_Cam.InSpace = false;
		s_Cam.Offset = {};
		s_Cam.Velocity = {};
		s_Cam.Look = s_Cam.LookTarget = { transform.Rotation.X, transform.Rotation.Y };
		s_Cam.Fov = s_Cam.FovTarget = s_Cam.GameFov = view.FieldOfView;
	}
	if (!s_Cam.On)
	{
		ShowInsideAndOut(world, transform.Position);
		return;
	}

	if (firstThisFrame)
	{
		if (Input::WasKeyPressed(SDL_SCANCODE_G))
		{
			s_Cam.Flying = !s_Cam.Flying;
			s_Cam.Velocity = {};
		}
		if (Input::WasKeyPressed(SDL_SCANCODE_V))
		{
			// Swap anchors without the camera jumping.
			const Vec3 at = Position();
			s_Cam.InSpace = !s_Cam.InSpace;
			s_Cam.Offset = s_Cam.InSpace ? at : at - s_Cam.Anchor;
		}

		Vec3 wish;
		if (s_Cam.Flying)
		{
			const Vec2 delta = Input::GetMouseDelta();
			s_Cam.LookTarget.Y = s_Cam.LookTarget.Y - delta.X * LookSensitivity;
			s_Cam.LookTarget.X = std::clamp(s_Cam.LookTarget.X - delta.Y * LookSensitivity, -89.0f, 89.0f);
			s_Cam.FovTarget = std::clamp(s_Cam.FovTarget - Input::GetScroll().Y * ZoomPerNotch, 10.0f, 110.0f);

			TransformComponent facing;
			facing.Rotation = { s_Cam.Look.X, s_Cam.Look.Y, 0 };
			const Vec2 axis = Input::GetMoveAxis();
			const float rise = (Input::IsKeyDown(SDL_SCANCODE_SPACE) ? 1.0f : 0.0f) - (Input::IsKeyDown(SDL_SCANCODE_LCTRL) ? 1.0f : 0.0f);
			wish = facing.GetForward() * axis.Y + facing.GetRight() * axis.X + Vec3{ 0, rise, 0 };
			if (wish.LengthSquared() > 0)
				wish = wish.Normalized() * MoveSpeed
					* (Input::IsKeyDown(SDL_SCANCODE_LSHIFT) ? FastScale : 1) * (Input::IsKeyDown(SDL_SCANCODE_LALT) ? SlowScale : 1);
		}
		s_Cam.Velocity = Lerp(s_Cam.Velocity, wish, 1 - std::exp(-MoveSmoothing * deltaTime));
		s_Cam.Offset += s_Cam.Velocity * deltaTime;
		s_Cam.Look = Lerp(s_Cam.Look, s_Cam.LookTarget, 1 - std::exp(-LookSmoothing * deltaTime));
		s_Cam.Fov = Damp(s_Cam.Fov, s_Cam.FovTarget, ZoomSmoothing, deltaTime);
	}

	transform.Position = s_Cam.Written = Position();
	transform.Rotation = { s_Cam.Look.X, s_Cam.Look.Y, 0 };
	view.FieldOfView = s_Cam.Fov;
	for (auto [entity, stars, starTransform] : world.View<StarfieldComponent, TransformComponent>().each())
		starTransform.Position = transform.Position;
	ShowInsideAndOut(world, transform.Position);
}
