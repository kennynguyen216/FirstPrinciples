#include "DriverSystem.h"

#include "WeatherFetch.h"

#include "Engine/UI/UISystem.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>

using namespace FP;

namespace
{
	struct DustComponent
	{
	};

	const Color White{ 1, 1, 1, 1 };
	const Color Dim{ 0.65f, 0.7f, 0.8f, 1 };
	const Color Good{ 0.4f, 1, 0.5f, 1 };
	const Color Warn{ 1, 0.8f, 0.25f, 1 };
	const Color Bad{ 1, 0.3f, 0.25f, 1 };

	std::string Format(const char* format, ...)
	{
		char text[256];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		return text;
	}

	float Wrap(float v, float spread)
	{
		return v - spread * std::floor(v / spread + 0.5f);
	}

	Entity FindOrNull(World& world, const char* name)
	{
		const Entity entity = world.FindEntityByName(name);
		return world.IsValid(entity) ? entity : NullEntity;
	}

	Color Faded(Color color, float alpha) { return { color.R, color.G, color.B, alpha }; }

	// One line of HUD text for the page: its text, CSS color, an optional bar filled 0 to 1 and a size.
	std::string Line(const std::string& text, Color color, float bar = -1, float size = 1)
	{
		auto channel = [](float c) { return static_cast<int>(std::clamp(c, 0.0f, 1.0f) * 255); };
		return "{\"t\":" + UIDocument::ToJsonString(text) + Format(",\"c\":\"rgba(%d,%d,%d,%.2f)\",\"bar\":%.3f,\"size\":%.2f}",
			channel(color.R), channel(color.G), channel(color.B), color.A, bar, size);
	}

	// Where a point lands on screen, -1 to 1 on both axes. False when it is off screen or behind.
	bool ToScreen(const TransformComponent& camera, float fieldOfView, float aspect, Vec3 point, Vec2& screen)
	{
		const Quat rotation = camera.GetRotation();
		const Vec3 local = Quat{ -rotation.X, -rotation.Y, -rotation.Z, rotation.W }.Rotate(point - camera.Position);
		if (local.Z >= 0)
		{
			screen = Vec2{ local.X, local.Y };
			return false;
		}
		const float halfHeight = std::tan(fieldOfView * 0.5f * DegToRad) * -local.Z;
		screen = Vec2{ local.X / aspect, local.Y } * (1 / halfHeight);
		return std::abs(screen.X) <= 1 && std::abs(screen.Y) <= 1;
	}
}

void DriverSystem::OnStart(World& world)
{
	m_Truck = FindOrNull(world, "Truck");
	if (m_Truck == NullEntity)
		return;
	if (!LoadDriverSettings(Paths::Content("Settings/Driver.settings").CStr(), m_Settings))
	{
		m_Truck = NullEntity;
		return;
	}

	m_Package = BuildMatchPackage(GatherMatchSources(m_Settings), m_Settings, static_cast<unsigned>(std::time(nullptr)));
	FP_LOG("Match Package: weather %s, %d solar wind readings (peak %.0f km/s), %d asteroid clusters (%s)",
		m_Package.WeatherLabel.c_str(), static_cast<int>(m_Package.SolarWind.size()), m_Package.PeakSolarWind,
		static_cast<int>(m_Package.Clusters.size()), m_Package.AsteroidLabel.c_str());

	TruckState truck;
	const TransformComponent& start = world.Get<TransformComponent>(m_Truck);
	truck.Position = start.Position;
	truck.Yaw = start.Rotation.Y;
	truck.Pitch = start.Rotation.X;
	truck.Travel = GetNose(truck);
	truck.Random = m_Package.Seed;

	m_Route = { truck.Position, truck.Travel, m_Settings.CruiseSpeed * m_Settings.CrossingTime };
	m_Colony = m_Route.Start + m_Route.Direction * (m_Route.Length + m_Settings.ColonyRadius);
	truck.Destination = m_Colony;
	if (const Entity colony = FindOrNull(world, "Colony"); colony != NullEntity)
	{
		TransformComponent& colonyTransform = world.Get<TransformComponent>(colony);
		colonyTransform.Position = m_Colony;
		colonyTransform.Scale = Vec3{ 1, 1, 1 } * (m_Settings.ColonyRadius * 2);
	}

	m_Rocks = BuildRocks(m_Package, m_Route, m_Settings);
	m_RockEntities.clear();
	for (const Rock& rock : m_Rocks)
	{
		const Entity entity = world.CreateEntity("Asteroid");
		world.Get<TransformComponent>(entity).Scale = Vec3{ 1, 1, 1 } * (rock.Radius * 2);
		world.Add<MeshComponent>(entity).Mesh = "Sphere";
		world.Add<MaterialComponent>(entity).Material = "Materials/Rock.material";
		m_RockEntities.push_back(entity);
	}

	// Placeholders: a black core inside a see-through ring that marks where the slingshot charges.
	m_BlackHoles = BuildBlackHoles(m_Package, m_Route, m_Settings);
	auto addSphere = [&](Vec3 position, float radius, const char* material) {
		const Entity entity = world.CreateEntity("BlackHole");
		TransformComponent& transform = world.Get<TransformComponent>(entity);
		transform.Position = position;
		transform.Scale = Vec3{ 1, 1, 1 } * (radius * 2);
		world.Add<MeshComponent>(entity).Mesh = "Sphere";
		world.Add<MaterialComponent>(entity).Material = material;
	};
	for (const BlackHole& hole : m_BlackHoles)
	{
		addSphere(hole.Position, hole.CoreRadius, "Materials/BlackHole.material");
		addSphere(hole.Position, hole.RingRadius, "Materials/BlackHoleRing.material");
	}

	m_Driver = FindOrNull(world, "Driver");
	const Entity floor = FindOrNull(world, "CabFloor");
	const Entity wheel = FindOrNull(world, "Wheel");
	const Entity scrubber = FindOrNull(world, "Scrubber");
	if (m_Driver != NullEntity && floor != NullEntity && wheel != NullEntity)
	{
		DriverState driver;
		driver.Position = world.Get<TransformComponent>(m_Driver).Position;
		driver.Yaw = world.Get<TransformComponent>(m_Driver).Rotation.Y;
		world.Add<DriverState>(m_Driver, driver);

		const TransformComponent& floorTransform = world.Get<TransformComponent>(floor);
		const Vec3 half = Vec3{ floorTransform.Scale.X, 0, floorTransform.Scale.Z } * 0.5f - Vec3{ m_Settings.CabWallMargin, 0, m_Settings.CabWallMargin };
		m_CabBounds = { floorTransform.Position - half, floorTransform.Position + half };
		// The hatch is an open doorway; the walker may step into it, where the cab's owner takes them
		// through (Dead Air Freight's TransitSystem).
		if (const Entity hatch = FindOrNull(world, "CabHatch"); hatch != NullEntity)
		{
			const TransformComponent& hatchTransform = world.Get<TransformComponent>(hatch);
			m_CabBounds.DoorX = hatchTransform.Position.X;
			m_CabBounds.DoorHalfWidth = hatchTransform.Scale.X * 0.5f - m_Settings.CabWallMargin;
			m_CabBounds.DoorZ = hatchTransform.Position.Z + 1;
		}
		m_Wheel = world.Get<TransformComponent>(wheel).Position;
		m_Scrubber = scrubber != NullEntity ? world.Get<TransformComponent>(scrubber).Position : Vec3{ 1e9f, 0, 0 };

		m_CabOrigin = floorTransform.Position;
		m_CabParts.clear();
		for (auto [entity, name, transform] : world.View<NameComponent, TransformComponent>().each())
		{
			const std::string_view partName = name.Name.CStr();
			if (partName.starts_with("Cab") || partName == "Wheel" || partName == "Scrubber")
				m_CabParts.push_back({ entity, transform.Position, transform.GetRotation() });
		}
		const Entity seat = FindOrNull(world, "CabSeat");
		const Vec3 seatAt = seat != NullEntity ? world.Get<TransformComponent>(seat).Position : driver.Position;
		m_SeatEye = { seatAt.X, m_CabOrigin.Y + m_Settings.SeatedEyeHeight, seatAt.Z };
		if (world.Has<MeshComponent>(m_Truck))
			world.Remove<MeshComponent>(m_Truck);
	}
	else
	{
		m_Driver = NullEntity;
	}
	m_TravelMarker = FindOrNull(world, "TravelMarker");
	m_CabLight = FindOrNull(world, "CabLight");
	if (m_CabLight != NullEntity && world.Has<PointLightComponent>(m_CabLight))
		m_CabLightIntensity = world.Get<PointLightComponent>(m_CabLight).Intensity;
	else
		m_CabLight = NullEntity;

	world.Add<TruckState>(m_Truck, truck);
	world.Add<LifeSupport>(m_Truck, StartLifeSupport(m_Settings));
	world.Add<Ticket>(m_Truck);

	unsigned dustRandom = 1;
	for (int32 i = 0; i < static_cast<int32>(m_Settings.DustCount); ++i)
	{
		auto random = [&] { return (NextRandom(dustRandom) - 0.5f) * m_Settings.DustSpread; };
		const Entity dust = world.CreateEntity("Dust");
		TransformComponent& transform = world.Get<TransformComponent>(dust);
		transform.Position = truck.Position + Vec3{ random(), random(), random() };
		transform.Scale = { 0.15f, 0.15f, 0.15f };
		world.Add<MeshComponent>(dust).Mesh = "Cube";
		world.Add<DustComponent>(dust);
	}

	m_CursorFree = false;
	m_Bank = 0;
	m_BaseFieldOfView = 60;
	m_HudCamera = NullEntity;
	m_NextHudSend = 0;
	m_LastHud.clear();
	for (auto [entity, camera] : world.View<CameraComponent>().each())
	{
		m_BaseFieldOfView = camera.Primary ? camera.FieldOfView : m_BaseFieldOfView;
		m_HudCamera = camera.Primary ? entity : m_HudCamera;
	}
	// The HUD is a page drawn over this player's own camera, so it shows on no one else's screen.
	if (m_HudCamera != NullEntity)
	{
		UIComponent& hud = world.Has<UIComponent>(m_HudCamera) ? world.Get<UIComponent>(m_HudCamera) : world.Add<UIComponent>(m_HudCamera);
		hud.Page = "UI/DriverHud.html";
		hud.Input = false; // the mouse flies the truck
	}
	m_PeakWindFought = 0;
	m_NextFault = m_Settings.StormFaultInterval;
	m_TicketsSinceJackpot = 0;
	m_Messages.clear();
	m_Results.clear();
	m_Finished = false;
	Notify(m_Package.WeatherLabel == "LIVE" ? "THAT'S TODAY'S REAL SOLAR WIND. FIGHT IT." : "REPLAYING REAL SPACE WEATHER FROM " + m_Package.WindowDate, Warn, 6);
}

void DriverSystem::Update(World& world, float deltaTime)
{
	TruckState* truck = m_Truck != NullEntity && world.IsValid(m_Truck) ? world.TryGet<TruckState>(m_Truck) : nullptr;
	if (!truck)
		return;
	LifeSupport& life = world.Get<LifeSupport>(m_Truck);
	Ticket& ticket = world.Get<Ticket>(m_Truck);
	DriverState* driver = m_Driver != NullEntity ? world.TryGet<DriverState>(m_Driver) : nullptr;

	if (!m_Finished)
		Simulate(*truck, life, ticket, driver, deltaTime);
	if (m_Finished)
	{
		truck->Speed = truck->Boost = 0;
		Input::SetCursorMode(CursorMode::Visible);
	}
	for (Message& message : m_Messages)
		message.TimeLeft -= deltaTime;
	m_Messages.erase(std::remove_if(m_Messages.begin(), m_Messages.end(), [](const Message& m) { return m.TimeLeft <= 0; }), m_Messages.end());

	Display(world, *truck, life, ticket, driver, deltaTime);
}

void DriverSystem::Simulate(TruckState& truck, LifeSupport& life, Ticket& ticket, DriverState* driver, float deltaTime)
{
	// Escape frees the cursor and a click takes it back.
	if (Input::WasKeyPressed(SDL_SCANCODE_ESCAPE))
		m_CursorFree = true;
	else if (Input::WasMousePressed(MouseButton::Left))
		m_CursorFree = false;
	Input::SetCursorMode(m_CursorFree ? CursorMode::Visible : CursorMode::Locked);
	const Vec2 mouseMove = Input::GetMouseDelta();

	const bool awake = !IsBlackedOut(life);
	bool repairing = false;
	if (driver && driver->InCab && awake)
	{
		if (Input::WasKeyPressed(SDL_SCANCODE_E) && !ticket.Active)
		{
			if (driver->Seated)
				driver->Seated = false;
			else if (CanReach(*driver, m_Wheel, m_Settings))
				driver->Seated = true;
		}
		repairing = !driver->Seated && Input::IsKeyDown(SDL_SCANCODE_E) && CanReach(*driver, m_Scrubber, m_Settings);
	}
	const bool seated = driver ? driver->Seated : true;
	if (seated && awake && Input::WasKeyPressed(SDL_SCANCODE_T) && !ticket.Active)
	{
		const Prize prize = WithPity(RollPrize(NextRandom(truck.Random), GetPrizeOdds(m_Settings)), m_TicketsSinceJackpot, m_Settings);
		if (BuyTicket(ticket, life, prize, m_Settings))
			m_TicketsSinceJackpot = prize == Prize::Jackpot ? 0 : m_TicketsSinceJackpot + 1;
		else
			Notify("NOT ENOUGH CARGO AIR FOR A TICKET", Bad, 2);
	}
	const bool scratching = ticket.Active && !ticket.Revealed;
	truck.AtWheel = seated && !scratching && awake;

	m_WindNow = SolarWindAt(m_Package, truck.Time, m_Settings);
	m_KpNow = KpAt(m_Package, truck.Time, m_Settings);
	m_PeakWindFought = std::max(m_PeakWindFought, m_WindNow);

	TruckControls controls;
	if (truck.AtWheel)
	{
		controls.Aim = mouseMove * m_Settings.MouseSensitivity;
	}
	const float yawBefore = truck.Yaw;
	StepTruck(truck, controls, { m_WindNow, m_KpNow }, GetDisturbanceScale(life, m_Settings), m_Settings, deltaTime);
	if (deltaTime > 0)
	{
		const float bank = std::clamp((truck.Yaw - yawBefore) / deltaTime * m_Settings.BankPerTurn, -m_Settings.BankMax, m_Settings.BankMax);
		m_Bank += (bank - m_Bank) * std::min(1.0f, m_Settings.BankSmoothing * deltaTime);
	}
	// A hit or a solar surge browns out the power and knocks out the scrubber, so someone has to
	// leave the wheel to fix it while the truck drifts.
	auto fault = [&](const char* what) {
		life.PowerFlicker = m_Settings.PowerFlickerTime;
		if (life.ScrubberBroken)
			return;
		const bool banked = life.AutoFix;
		BreakScrubber(life);
		Notify(banked ? "FREE FIX SAVED THE SCRUBBER" : what, banked ? Good : Bad, 2.5f);
	};
	if (CollideWithRocks(truck, m_Rocks, truck.Time, m_Settings))
	{
		Notify("ASTEROID HIT!", Bad, 1.5f);
		fault("THE HIT KNOCKED OUT THE SCRUBBER");
	}
	switch (StepBlackHoles(truck, m_BlackHoles, m_Settings, deltaTime))
	{
	case BlackHoleEvent::Slingshot:
		Notify("SLINGSHOT! HYPERDRIVE OVERLOAD", Good, 3);
		break;
	case BlackHoleEvent::Swallowed:
		Notify("TOO DEEP! THE BLACK HOLE SPAT YOU OUT", Bad, 2);
		fault("THE TIDES KNOCKED OUT THE SCRUBBER");
		break;
	case BlackHoleEvent::None:
		break;
	}
	// Surges come as often as the real Kp says, the same storm that jerks the truck.
	m_NextFault -= deltaTime;
	if (m_NextFault <= 0)
	{
		const float storm = MapClamped(m_KpNow, m_Settings.CalmKp, m_Settings.StormKp, 0, 1);
		m_NextFault = (m_Settings.CalmFaultInterval + (m_Settings.StormFaultInterval - m_Settings.CalmFaultInterval) * storm) * (0.5f + NextRandom(truck.Random));
		fault("SOLAR SURGE FRIED THE SCRUBBER");
	}

	const bool scrubberWasBroken = life.ScrubberBroken;
	StepLifeSupport(life, repairing, m_Settings, deltaTime);
	if (scrubberWasBroken && !life.ScrubberBroken)
		Notify("SCRUBBER FIXED", Good, 2.5f);
	if (StepTicket(ticket, life, ticket.Active && awake ? mouseMove : Vec2{}, awake && Input::IsMouseDown(MouseButton::Left), m_Settings, deltaTime))
	{
		FP_LOG("Ticket: %s", GetPrizeName(ticket.Won));
		if (ticket.Won == Prize::Jackpot)
		{
			truck.Boost = m_Settings.BoostTime;
			Notify("JACKPOT! HYPERDRIVE OVERLOAD", Good, 3);
		}
	}

	if (driver && driver->InCab && !driver->Seated && awake)
		StepOnFoot(*driver, mouseMove * m_Settings.LookSensitivity, Input::GetMoveAxis(), m_CabBounds, m_Settings, deltaTime);

	if (GetAltitude(truck) <= m_Settings.DeliveryAltitude)
		Finish("DELIVERED", truck, life);
	else if (life.ColonyTimeLeft <= 0)
		Finish("THE COLONY RAN OUT OF AIR", truck, life);
	else if (life.Cargo <= 0)
		Finish(ticket.Active ? "YOU GAMBLED AWAY THE COLONY'S LAST AIR" : "CARGO EMPTY: NOTHING LEFT TO DELIVER", truck, life);
}

void DriverSystem::Finish(const char* reason, const TruckState& truck, const LifeSupport& life)
{
	m_Finished = true;
	const bool delivered = std::string(reason) == "DELIVERED";
	const float cargo = delivered ? life.Cargo : 0;
	const float bonus = delivered ? std::max(0.0f, m_Settings.ParTime - truck.Time) * m_Settings.TimeBonusPerSecond : 0;
	m_Results = {
		reason,
		Format("CARGO OXYGEN DELIVERED  %.1f KG", cargo),
		Format("TIME BONUS  +%.1f    SCORE  %.1f", bonus, cargo + bonus),
		Format("TIME  %d:%02d    TICKETS  %d  (%.1f KG OF AIR)", static_cast<int>(truck.Time) / 60, static_cast<int>(truck.Time) % 60, life.Tickets, life.TicketSpend),
		"",
		"REAL DATA YOU FLEW THROUGH",
		Format("THE SOLAR WIND YOU FOUGHT PEAKED AT %.0f KM/S", m_PeakWindFought),
		"  NOAA SWPC REAL-TIME SOLAR WIND, " + m_Package.WindowDate + (m_Package.WeatherLabel == "LIVE" ? " (LIVE)" : " (SAVED)"),
	};
	if (!m_Package.Clusters.empty())
	{
		const AsteroidCluster* closest = &m_Package.Clusters.front();
		for (const AsteroidCluster& cluster : m_Package.Clusters)
			closest = cluster.MissLunar < closest->MissLunar ? &cluster : closest;
		m_Results.push_back(Format("%d ASTEROIDS ARE REAL FLYBYS. CLOSEST: %s AT %.1f LUNAR DISTANCES",
			static_cast<int>(m_Package.Clusters.size()), closest->Name.c_str(), closest->MissLunar));
		m_Results.push_back("  NASA NEOWS, " + m_Package.AsteroidLabel);
	}
	for (const std::string& line : m_Results)
		FP_LOG("Results: %s", line.c_str());
}

void DriverSystem::Notify(const std::string& text, Color color, float seconds)
{
	m_Messages.push_back({ text, color, seconds });
}

Quat DriverSystem::GetTruckRotation(const TruckState& truck) const
{
	return Quat::FromEuler({ truck.Pitch, truck.Yaw, m_Bank });
}

float DriverSystem::GetAltitude(const TruckState& truck) const
{
	return (truck.Position - m_Colony).Length() - m_Settings.ColonyRadius;
}

float DriverSystem::GetSpeedRatio(const TruckState& truck) const
{
	return truck.Speed / std::max(m_Settings.CruiseSpeed, 1.0f);
}

Vec3 DriverSystem::CabToWorld(const TruckState& truck, Vec3 cabPoint) const
{
	return truck.Position + GetTruckRotation(truck).Rotate(cabPoint - m_CabOrigin);
}

void DriverSystem::Display(World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver, float deltaTime)
{
	TransformComponent& transform = world.Get<TransformComponent>(m_Truck);
	transform.Position = truck.Position;
	transform.Rotation = { truck.Pitch, truck.Yaw, m_Bank };

	const Quat truckRotation = GetTruckRotation(truck);
	for (const CabPart& part : m_CabParts)
	{
		if (!world.IsValid(part.Entity))
			continue;
		TransformComponent& partTransform = world.Get<TransformComponent>(part.Entity);
		partTransform.Position = CabToWorld(truck, part.Position);
		partTransform.Rotation = (truckRotation * part.Rotation).ToEuler();
	}

	if (m_TravelMarker != NullEntity && world.IsValid(m_TravelMarker))
		world.Get<TransformComponent>(m_TravelMarker).Position = truck.Position + truck.Travel * 25.0f;

	for (size_t i = 0; i < m_Rocks.size() && i < m_RockEntities.size(); ++i)
	{
		if (world.IsValid(m_RockEntities[i]))
			world.Get<TransformComponent>(m_RockEntities[i]).Position = GetRockPosition(m_Rocks[i], truck.Time);
	}

	Vec3 streakAxis = Vec3::Cross({ 0, 0, -1 }, truck.Travel);
	const float streakAngle = std::acos(std::clamp(-truck.Travel.Z, -1.0f, 1.0f)) * RadToDeg;
	const Vec3 streakRotation = (streakAxis.LengthSquared() > 1e-8f ? Quat::AxisAngle(streakAxis, streakAngle) : Quat{}).ToEuler();
	const float speedRatio = GetSpeedRatio(truck);
	const float streak = 0.15f + m_Settings.StreakLength * speedRatio;
	const Vec3 rush = truck.Travel * (-m_Settings.StreakSpeed * speedRatio * deltaTime);
	const float clearance = m_Settings.TruckRadius * 2;
	for (auto [entity, dustTransform] : world.View<DustComponent, TransformComponent>().each())
	{
		Vec3 offset = dustTransform.Position + rush - truck.Position;
		const Vec3 side = offset - truck.Travel * Vec3::Dot(offset, truck.Travel);
		const float sideDistance = side.Length();
		if (sideDistance < clearance)
			offset += (sideDistance > 1e-4f ? side * (1 / sideDistance) : Vec3::Cross(truck.Travel, { 0, 1, 0 }).Normalized()) * (clearance - sideDistance);
		dustTransform.Position = truck.Position + Vec3{ Wrap(offset.X, m_Settings.DustSpread),
			Wrap(offset.Y, m_Settings.DustSpread), Wrap(offset.Z, m_Settings.DustSpread) };
		dustTransform.Rotation = streakRotation;
		dustTransform.Scale.Z = streak;
	}

	if (m_CabLight != NullEntity)
		world.Get<PointLightComponent>(m_CabLight).Intensity = m_CabLightIntensity * life.Power;

	if (driver)
		world.Get<TransformComponent>(m_Driver).Position = CabToWorld(truck, driver->Position);

	DrawHud(world, truck, life, ticket, driver);
}

void DriverSystem::DrawHud(World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver)
{
	const bool seated = driver ? driver->Seated : true;
	const Quat truckRotation = GetTruckRotation(truck);
	TransformComponent* cameraTransform = nullptr;
	float fieldOfView = 60;
	for (auto [entity, camera, cameraTransformIt] : world.View<CameraComponent, TransformComponent>().each())
	{
		if (!camera.Primary)
			continue;
		if (!driver)
		{
			cameraTransformIt.Position = truck.Position - GetNose(truck) * m_Settings.ChaseDistance
				+ truckRotation.Rotate({ 0, m_Settings.ChaseHeight, 0 });
			cameraTransformIt.Rotation = { truck.Pitch, truck.Yaw, m_Bank };
		}
		else if (!driver->InCab)
		{
			// Down in the cargo bay, where Dead Air Freight's OnFootSystem places the camera.
		}
		else if (seated)
		{
			cameraTransformIt.Position = CabToWorld(truck, m_SeatEye);
			cameraTransformIt.Rotation = { truck.Pitch, truck.Yaw, m_Bank };
		}
		else
		{
			cameraTransformIt.Position = CabToWorld(truck, driver->Position + Vec3{ 0, m_Settings.EyeHeight, 0 });
			cameraTransformIt.Rotation = (truckRotation * Quat::FromEuler({ driver->Pitch, driver->Yaw, 0 })).ToEuler();
		}
		const float overdrive = m_Settings.BoostSpeedScale > 1 ? std::clamp((GetSpeedRatio(truck) - 1) / (m_Settings.BoostSpeedScale - 1), 0.0f, 1.0f) : 0;
		camera.FieldOfView = m_BaseFieldOfView + m_Settings.BoostFov * overdrive;
		const float shake = m_Settings.BoostShake * overdrive;
		const float phase = truck.Time * 2 * Pi;
		cameraTransformIt.Rotation += Vec3{ std::sin(phase * 23) + 0.5f * std::sin(phase * 37 + 1), std::sin(phase * 19 + 2) + 0.5f * std::sin(phase * 41), 0 } * (shake / 1.5f);
		cameraTransform = &cameraTransformIt;
		fieldOfView = camera.FieldOfView;
	}
	if (!cameraTransform)
		return;

	UIDocumentInstance* page = m_HudCamera != NullEntity && world.IsValid(m_HudCamera) ? UISystem::For(world, m_HudCamera) : nullptr;
	if (!page || !page->IsReady())
		return;
	// The page relays out everything it is sent, so it hears from us 20 times a second, and every
	// frame only while a ticket is being scratched so the coin keeps up with the mouse.
	const bool scratching = ticket.Active && !ticket.Revealed;
	if (!scratching && world.GetTime() < m_NextHudSend)
		return;
	m_NextHudSend = world.GetTime() + 0.05;
	const UIStateComponent* pageState = world.TryGet<UIStateComponent>(m_HudCamera);
	const float aspect = pageState && pageState->Height > 0 ? static_cast<float>(pageState->Width) / static_cast<float>(pageState->Height) : 16.0f / 9;
	const bool flash = std::fmod(truck.Time, 0.5f) < 0.25f;

	// Everything the page draws, as one JSON object. The page only lays it out (Content/UI/DriverHud.html).
	std::string json = "{";
	auto field = [&](const char* name, const std::string& value) {
		json += json.size() > 1 ? "," : "";
		json += Format("\"%s\":", name) + value;
	};
	auto list = [](const std::vector<std::string>& items) {
		std::string out = "[";
		for (size_t i = 0; i < items.size(); ++i)
			out += (i ? "," : "") + items[i];
		return out + "]";
	};
	field("dark", Format("%.3f", IsBlackedOut(life) ? 1 : life.Co2 * m_Settings.Co2DarkenMax));

	const Color co2Color = life.Co2 > 0.6f ? Bad : life.Co2 > 0.25f ? Warn : Good;
	std::vector<std::string> left = {
		Line(Format("CAB AIR   %.2f KG", life.CabReserve), life.CabReserve > 0 ? White : Warn),
		Line(Format("CARGO O2  %.1f KG", life.Cargo), White),
		Line(Format("CO2       %3.0f%%", life.Co2 * 100), co2Color, life.Co2),
		Line(Format("POWER     %3.0f%%", life.Power * 100), life.Power < 1 ? Warn : White),
	};
	if (life.CalmHands > 0)
		left.push_back(Line(Format("CALM HANDS %.0fS", std::ceil(life.CalmHands)), Good));
	if (life.AutoFix)
		left.push_back(Line("FREE FIX BANKED", Good));
	if (truck.Boost > 0)
		left.push_back(Line(Format("HYPERDRIVE %.0fS", std::ceil(truck.Boost)), flash ? Good : White));
	for (const BlackHole& hole : m_BlackHoles)
	{
		if (!hole.Spent && hole.Charge > 0)
			left.push_back(Line(Format("SLINGSHOT  %3.0f%%", hole.Charge / std::max(m_Settings.SlingshotTime, 1e-3f) * 100), Warn, hole.Charge / std::max(m_Settings.SlingshotTime, 1e-3f)));
	}
	field("left", list(left));

	const int clock = static_cast<int>(std::ceil(life.ColonyTimeLeft));
	field("clock", Line(Format("COLONY AIR %d:%02d", clock / 60, clock % 60), life.ColonyTimeLeft < 60 && flash ? Bad : White));
	field("right", list({
		Line(Format("SOLAR WIND %.0f KM/S", m_WindNow), White),
		Line(Format("STORM KP %.1f", m_KpNow), White),
		Line(m_Package.WeatherLabel, m_Package.WeatherLabel == "LIVE" ? Good : Warn),
	}));

	std::vector<std::string> center;
	if (life.ScrubberBroken)
		center.push_back(Line("SCRUBBER DOWN", flash ? Bad : White, -1, 1.4f));
	for (const Message& message : m_Messages)
		center.push_back(Line(message.Text, Faded(message.Color, std::min(message.TimeLeft, 1.0f))));
	field("center", list(center));
	field("blackout", IsBlackedOut(life) ? "true" : "false");

	std::vector<std::string> prompts;
	if (!m_Finished && !IsBlackedOut(life))
	{
		if (ticket.Active)
		{
			if (!ticket.Revealed)
				prompts.push_back(Line("HOLD LEFT MOUSE AND DRAG TO SCRATCH. HANDS ARE OFF THE WHEEL!", Dim));
		}
		else if (seated)
		{
			prompts.push_back(Line(Format("MOUSE AIM  [T] TICKET -%.1fKG%s", GetTicketCost(m_Settings), driver ? "  [E] STAND UP" : ""), Dim));
		}
		else if (driver)
		{
			if (CanReach(*driver, m_Wheel, m_Settings))
				prompts.push_back(Line("[E] TAKE THE WHEEL", White));
			else if (CanReach(*driver, m_Scrubber, m_Settings))
				prompts.push_back(Line(life.ScrubberBroken ? Format("HOLD [E] TO REPAIR THE SCRUBBER  %.0f%%", life.RepairProgress / m_Settings.ScrubberRepairTime * 100) : "AIR SCRUBBER: WORKING",
					life.ScrubberBroken ? Warn : Good));
		}
		if (!truck.AtWheel && !ticket.Active)
		{
			const float drift = GetDriftAmount(truck, m_Settings);
			prompts.push_back(Line(drift > 0 ? Format("NOBODY AT THE WHEEL: DRIFTING %.0f%%", drift * 100) : Format("NOBODY AT THE WHEEL: HOLDING STEADY %.0fS", std::ceil(m_Settings.SteadyTime - truck.WheelEmptyTime)),
				drift > 0 ? Bad : Warn));
		}
	}
	field("prompts", list(prompts));

	const Vec3 toColony = m_Colony - truck.Position;
	const float altitude = std::max(GetAltitude(truck), 0.0f);
	const float km = altitude * m_Settings.KmPerUnit;
	const int eta = static_cast<int>(std::ceil(altitude / std::max({ truck.Speed, m_Settings.CruiseSpeed, 1.0f })));
	const float offCourse = std::acos(std::clamp(Vec3::Dot(truck.Travel, toColony.Normalized()), -1.0f, 1.0f)) * RadToDeg;
	const Color courseColor = offCourse < 10 ? Good : offCourse < 35 ? Warn : Bad;
	field("progress", Format("%.4f", std::clamp(1 - altitude / m_Route.Length, 0.0f, 1.0f)));
	field("distance", Line((km >= 1e6f ? Format("%.1f MILLION KM", km / 1e6f) : Format("%.0f KM", km)) + Format("   ETA %d:%02d", eta / 60, eta % 60), White));
	field("course", Line(Format("OFF COURSE %.0f DEG    SPEED %.2fC", offCourse, truck.Speed * m_Settings.KmPerUnit / 299792.458f), courseColor, std::min(offCourse / 90, 1.0f)));

	// Points at the colony from the screen's edge while it is out of view.
	Vec2 screen;
	if (seated && !ToScreen(*cameraTransform, fieldOfView, aspect, m_Colony, screen))
	{
		const float length = std::sqrt(screen.X * screen.X + screen.Y * screen.Y);
		const Vec2 direction = length > 1e-6f ? screen * (1 / length) : Vec2{ 0, -1 };
		field("arrow", Format("{\"x\":%.3f,\"y\":%.3f}", direction.X, direction.Y));
	}

	if (ticket.Active)
	{
		std::string coating(ticket.Coating.size(), '0');
		for (size_t i = 0; i < ticket.Coating.size(); ++i)
			coating[i] = ticket.Coating[i] ? '1' : '0';
		field("ticket", Format("{\"cols\":%d,\"rows\":%d,\"x\":%.3f,\"y\":%.3f,\"revealed\":%s,\"prize\":", ticket.Columns, ticket.Rows,
			ticket.Coin.X / std::max(ticket.Columns, 1), ticket.Coin.Y / std::max(ticket.Rows, 1), ticket.Revealed ? "true" : "false")
			+ UIDocument::ToJsonString(GetPrizeName(ticket.Won)) + ",\"coating\":\"" + coating + "\"}");
	}

	if (m_Finished)
	{
		std::vector<std::string> results;
		for (size_t i = 0; i < m_Results.size(); ++i)
			results.push_back(Line(m_Results[i], i == 0 ? Warn : m_Results[i].rfind("  ", 0) == 0 ? Dim : White, -1, i == 0 ? 1.8f : 0.9f));
		field("results", list(results));
	}
	json += "}";
	if (json == m_LastHud)
		return;
	page->SetJson("hud", json);
	m_LastHud = std::move(json);
}
