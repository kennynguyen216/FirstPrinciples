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
	// The windshield in the SpaceTruck model, before the Truck entity's scale. The portal sits just
	// outside it, as wide as its narrower bottom edge.
	const Vec3 WindshieldCenter{ 0.008f, 0.437f, -1.31f };
	const Vec3 WindshieldSize{ 1.64f, 0.83f, 1 };
	// Where the rooms wait while the camera is outside: far below the race, past the camera's far clip.
	const Vec3 Away{ 0, -40000, 0 };

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
		std::unordered_map<Entity, Vec3> Shifted; // inside parts sent Away, and where we put them
		Entity Portal = NullEntity, PortalExit = NullEntity;
		MeshComponent TruckMesh;     // the model, kept from before DriverSystem takes it off the truck
		MaterialComponent TruckLook;
		bool HasTruckMesh = false;
		bool ShellShown = false;
		Entity Self = NullEntity; // this player's character while they are in the cab
	} s_Cam;

	// H hides the speed lines and black holes, for recording.
	struct
	{
		bool On = false;
		std::unordered_map<Entity, MeshComponent> Meshes; // taken off, to put back
		std::unordered_map<Entity, BlackHoleComponent> Holes;
	} s_Plate;

	void StripScene(World& world)
	{
		// Every frame, since the game keeps making and dressing entities.
		std::vector<Entity> strip;
		for (auto [entity, mesh, name] : world.View<MeshComponent, NameComponent>().each())
			if (std::string_view(name.Name.CStr()) == "Dust")
				strip.push_back(entity);
		for (Entity entity : strip)
		{
			s_Plate.Meshes[entity] = world.Get<MeshComponent>(entity);
			world.Remove<MeshComponent>(entity);
		}
		std::vector<Entity> holes;
		for (auto [entity, hole] : world.View<BlackHoleComponent>().each())
			holes.push_back(entity);
		for (Entity entity : holes)
		{
			s_Plate.Holes[entity] = world.Get<BlackHoleComponent>(entity);
			world.Remove<BlackHoleComponent>(entity);
		}
	}

	void RestoreScene(World& world)
	{
		for (auto& [entity, mesh] : s_Plate.Meshes)
			if (world.IsValid(entity) && !world.Has<MeshComponent>(entity))
				world.Add<MeshComponent>(entity, mesh);
		for (auto& [entity, hole] : s_Plate.Holes)
			if (world.IsValid(entity))
				world.Add<BlackHoleComponent>(entity, hole);
		s_Plate = decltype(s_Plate){};
	}

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
			|| name == "Ghost" || name == "Crewmate In Cab" || name == "FreeCam Self";
	}

	// The windshield is a portal into the cab, which waits Away while the camera is outside.
	void PlacePortal(World& world, Entity truck, bool show)
	{
		if (!show)
		{
			for (Entity* entity : { &s_Cam.Portal, &s_Cam.PortalExit })
			{
				if (world.IsValid(*entity))
					world.DestroyEntity(*entity);
				*entity = NullEntity;
			}
			return;
		}
		if (!world.IsValid(s_Cam.Portal))
		{
			s_Cam.Portal = world.CreateEntity("FreeCam Portal");
			world.Add<MeshComponent>(s_Cam.Portal).Mesh = "Quad";
			world.Add<MaterialComponent>(s_Cam.Portal).Material = "Materials/Windshield.material";
			world.Add<PortalComponent>(s_Cam.Portal).Target = "FreeCam Portal Exit";
			s_Cam.PortalExit = world.CreateEntity("FreeCam Portal Exit");
		}
		// Fetched after creating entities, which can move the component storage.
		const TransformComponent& hull = world.Get<TransformComponent>(truck);
		TransformComponent& portal = world.Get<TransformComponent>(s_Cam.Portal);
		portal.Position = hull.Position + hull.GetRotation().Rotate(WindshieldCenter * hull.Scale);
		// The quad faces +Z, and the windshield faces the truck's front, -Z.
		portal.Rotation = (hull.GetRotation() * Quat::FromEuler({ 0, 180, 0 })).ToEuler();
		portal.Scale = WindshieldSize * hull.Scale;
		TransformComponent& exit = world.Get<TransformComponent>(s_Cam.PortalExit);
		exit.Position = portal.Position + Away;
		exit.Rotation = portal.Rotation;
		exit.Scale = portal.Scale;
	}

	void ShowOutside(World& world, Entity truck, bool outside)
	{
		if (outside)
		{
			for (auto [entity, name, transform] : world.View<NameComponent, TransformComponent>().each())
			{
				if (!IsInsidePart(name.Name.CStr()) || !(world.Has<MeshComponent>(entity) || world.Has<PointLightComponent>(entity)))
					continue;
				// Most parts are put back in the truck every frame. One still where we left it is already Away.
				const auto shifted = s_Cam.Shifted.find(entity);
				if (shifted == s_Cam.Shifted.end() || !(transform.Position == shifted->second))
					transform.Position += Away;
				s_Cam.Shifted[entity] = transform.Position;
			}
		}
		else
		{
			for (auto [entity, at] : s_Cam.Shifted)
			{
				if (!world.IsValid(entity))
					continue;
				TransformComponent& transform = world.Get<TransformComponent>(entity);
				if (transform.Position == at)
					transform.Position -= Away;
			}
			s_Cam.Shifted.clear();
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
		PlacePortal(world, truck, s_Cam.ShellShown);
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
		PlaceSelf(world, transform, s_Cam.On && riding);
		if (outside || s_Cam.ShellShown || !s_Cam.Shifted.empty())
			ShowOutside(world, truck, outside);
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

void FreeCamSystem::OnStop(World&)
{
	// The camera outlives a play session in the editor, and its entities belong to the old world.
	// Not on start: the driver launcher adds the second system after the truck's model is kept.
	s_Cam = decltype(s_Cam){};
	s_Plate = decltype(s_Plate){};
}

void FreeCamSystem::Update(World& world, float deltaTime)
{
	UpdateCamera(world, deltaTime);
	if (s_Plate.On)
		StripScene(world);
}

void FreeCamSystem::UpdateCamera(World& world, float deltaTime)
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
	if (firstThisFrame && Input::WasKeyPressed(SDL_SCANCODE_H))
	{
		s_Plate.On = !s_Plate.On;
		if (!s_Plate.On)
			RestoreScene(world);
	}
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
