#include "DriverSystem.h"

#include "WeatherFetch.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>

using namespace FP;

namespace
{
	// Tag for the specks of dust spawned around the truck. Not registered, so never saved.
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

	// Keeps v within half of spread on each axis, so the dust box always surrounds the truck.
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

	// The host builds one Match Package; both trucks play from it. Networking would send ToText(m_Package).
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

	// The straight-line route is as long as CrossingTime at cruise speed, ahead of where the truck starts.
	m_Route = { truck.Position, truck.Travel, m_Settings.CruiseSpeed * m_Settings.CrossingTime };
	m_Colony = m_Route.Start + m_Route.Direction * m_Route.Length;
	if (const Entity colony = FindOrNull(world, "Colony"); colony != NullEntity)
		world.Get<TransformComponent>(colony).Position = m_Colony;

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

		// The cab floor is a Plane mesh: 1x1 scaled by its transform, ignoring its rotation.
		const TransformComponent& floorTransform = world.Get<TransformComponent>(floor);
		const Vec3 half = Vec3{ floorTransform.Scale.X, 0, floorTransform.Scale.Z } * 0.5f - Vec3{ m_Settings.CabWallMargin, 0, m_Settings.CabWallMargin };
		m_CabBounds = { floorTransform.Position - half, floorTransform.Position + half };
		m_Wheel = world.Get<TransformComponent>(wheel).Position;
		// Without a scrubber in the scene it can't be reached, so only a Free Fix repairs it.
		m_Scrubber = scrubber != NullEntity ? world.Get<TransformComponent>(scrubber).Position : Vec3{ 1e9f, 0, 0 };

		// ponytail: the engine has no parenting, so the cab parts are moved with the truck by hand. See EngineRequests.md.
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
		// The cab is the truck now; the truck's own block would cut through it.
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
		transform.Scale = { 0.3f, 0.3f, 0.3f };
		world.Add<MeshComponent>(dust).Mesh = "Cube";
		world.Add<DustComponent>(dust);
	}

	m_LastMouse = Input::GetMousePosition();
	m_Bank = 0;
	m_PeakWindFought = 0;
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
	for (Message& message : m_Messages)
		message.TimeLeft -= deltaTime;
	m_Messages.erase(std::remove_if(m_Messages.begin(), m_Messages.end(), [](const Message& m) { return m.TimeLeft <= 0; }), m_Messages.end());

	Display(world, *truck, life, ticket, driver);
}

void DriverSystem::Simulate(TruckState& truck, LifeSupport& life, Ticket& ticket, DriverState* driver, float deltaTime)
{
	// ponytail: the engine can't lock the cursor yet, so mouse movement stops at the view's edge. See EngineRequests.md.
	// The first reading, and the cursor entering the view, jump the position; that isn't movement.
	const Vec2 mouse = Input::GetMousePosition();
	Vec2 mouseMove = mouse - m_LastMouse;
	if (std::abs(mouseMove.X) > m_Settings.MouseJumpLimit || std::abs(mouseMove.Y) > m_Settings.MouseJumpLimit)
		mouseMove = {};
	m_LastMouse = mouse;

	// ---- Driver input: nothing works while blacked out ----
	const bool awake = !IsBlackedOut(life);
	bool repairing = false;
	if (driver && awake)
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
		const Prize prize = RollPrize(NextRandom(truck.Random), GetPrizeOdds(m_Settings));
		if (!BuyTicket(ticket, life, prize, m_Settings))
			Notify("NOT ENOUGH CARGO AIR FOR A TICKET", Bad, 2);
	}
	// Scratching a ticket or blacking out takes your hands off the wheel.
	truck.AtWheel = seated && !ticket.Active && awake;

	// ---- Space weather: the 24 hour timelapse ----
	m_WindNow = SolarWindAt(m_Package, truck.Time, m_Settings);
	m_KpNow = KpAt(m_Package, truck.Time, m_Settings);
	m_PeakWindFought = std::max(m_PeakWindFought, m_WindNow);

	// ---- Flight ----
	TruckControls controls;
	if (truck.AtWheel)
	{
		controls.Aim = mouseMove * m_Settings.MouseSensitivity;
		controls.Thrust = Input::IsKeyDown(SDL_SCANCODE_W);
		controls.Brake = Input::IsKeyDown(SDL_SCANCODE_S);
	}
	const float yawBefore = truck.Yaw;
	StepTruck(truck, controls, { m_WindNow, m_KpNow }, GetDisturbanceScale(life, m_Settings), m_Settings, deltaTime);
	// Lean into turns so the cab visibly tilts. Display only: the flight itself has no roll.
	if (deltaTime > 0)
	{
		const float bank = std::clamp((truck.Yaw - yawBefore) / deltaTime * m_Settings.BankPerTurn, -m_Settings.BankMax, m_Settings.BankMax);
		m_Bank += (bank - m_Bank) * std::min(1.0f, m_Settings.BankSmoothing * deltaTime);
	}
	if (CollideWithRocks(truck, m_Rocks, truck.Time, m_Settings))
		Notify("ASTEROID HIT!", Bad, 1.5f);

	// ---- Life support and tickets ----
	const bool scrubberWasBroken = life.ScrubberBroken;
	StepLifeSupport(life, repairing, m_Settings, deltaTime);
	if (scrubberWasBroken && !life.ScrubberBroken)
		Notify("SCRUBBER FIXED", Good, 2.5f);
	// Blacked out, the coin stays put; the ticket still finishes showing its prize.
	if (StepTicket(ticket, life, ticket.Active && awake ? mouseMove : Vec2{}, awake && Input::IsMouseDown(MouseButton::Left), m_Settings, deltaTime))
		FP_LOG("Ticket: %s", GetPrizeName(ticket.Won));

	if (driver && !driver->Seated && awake)
		StepOnFoot(*driver, mouseMove * m_Settings.LookSensitivity, Input::GetMoveAxis(), m_CabBounds, m_Settings, deltaTime);

	// ---- End of the run ----
	if ((truck.Position - m_Colony).Length() <= m_Settings.DeliveryRadius)
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
	const float bonus = delivered ? life.ColonyTimeLeft * m_Settings.TimeBonusPerSecond : 0;
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

Vec3 DriverSystem::CabToWorld(const TruckState& truck, Vec3 cabPoint) const
{
	return truck.Position + GetTruckRotation(truck).Rotate(cabPoint - m_CabOrigin);
}

void DriverSystem::Display(World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver)
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

	for (auto [entity, dustTransform] : world.View<DustComponent, TransformComponent>().each()) // empty tags aren't listed
	{
		const Vec3 offset = dustTransform.Position - truck.Position;
		dustTransform.Position = truck.Position + Vec3{ Wrap(offset.X, m_Settings.DustSpread),
			Wrap(offset.Y, m_Settings.DustSpread), Wrap(offset.Z, m_Settings.DustSpread) };
	}

	if (m_CabLight != NullEntity)
		world.Get<PointLightComponent>(m_CabLight).Intensity = m_CabLightIntensity * life.Power;

	if (driver)
		world.Get<TransformComponent>(m_Driver).Position = CabToWorld(truck, driver->Position);

	DrawHud(world, truck, life, ticket, driver);
}

void DriverSystem::DrawHud(World& world, const TruckState& truck, const LifeSupport& life, const Ticket& ticket, const DriverState* driver)
{
	// Seated: out through the windshield. On foot: the Driver's eyes in the cab. No cab: chase camera.
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
		cameraTransform = &cameraTransformIt;
		fieldOfView = camera.FieldOfView;
	}
	if (!cameraTransform)
		return;

	Hud& hud = m_Hud;
	hud.Begin(*cameraTransform, fieldOfView, m_Settings);
	const float a = hud.GetAspect();
	const float t = m_Settings.HudTextSize;
	const float line = t * 1.5f;
	const bool flash = std::fmod(truck.Time, 0.5f) < 0.25f;

	// CO2 darkens the view; a blackout takes it all.
	const float dark = IsBlackedOut(life) ? 1 : life.Co2 * m_Settings.Co2DarkenMax;
	if (dark > 0.01f)
		hud.Box(world, { 0, 0 }, { a * 2 + 0.5f, 2.5f }, { 0, 0, 0, dark }, 5);

	// ---- Top left: air ----
	const float left = -a + 0.05f;
	float y = 1 - line;
	hud.Text(world, { left, y }, t, life.CabReserve > 0 ? White : Warn, Format("CAB AIR   %.2f KG", life.CabReserve));
	y -= line;
	hud.Text(world, { left, y }, t, White, Format("CARGO O2  %.1f KG", life.Cargo));
	y -= line;
	const Color co2Color = life.Co2 > 0.6f ? Bad : life.Co2 > 0.25f ? Warn : Good;
	hud.Text(world, { left, y }, t, co2Color, Format("CO2       %3.0f%%", life.Co2 * 100));
	const float co2Bar = left + t * 0.75f * 15;
	hud.Box(world, { co2Bar + 0.15f, y }, { 0.3f, t * 0.6f }, { 0.15f, 0.15f, 0.2f, 0.8f }, 10);
	hud.Box(world, { co2Bar + life.Co2 * 0.15f, y }, { life.Co2 * 0.3f, t * 0.6f }, co2Color, 11);
	y -= line;
	hud.Text(world, { left, y }, t, life.Power < 1 ? Warn : White, Format("POWER     %3.0f%%", life.Power * 100));
	if (life.CalmHands > 0)
		hud.Text(world, { left, y -= line }, t, Good, Format("CALM HANDS %.0fS", std::ceil(life.CalmHands)));
	if (life.AutoFix)
		hud.Text(world, { left, y -= line }, t, Good, "FREE FIX BANKED");

	// ---- Top center: the colony's countdown ----
	const int clock = static_cast<int>(std::ceil(life.ColonyTimeLeft));
	hud.Text(world, { 0, 1 - line * 1.2f }, t * 1.6f, life.ColonyTimeLeft < 60 && flash ? Bad : White, Format("COLONY AIR %d:%02d", clock / 60, clock % 60), Hud::Align::Center);

	// ---- Top right: live space weather ----
	const float right = a - 0.05f;
	hud.Text(world, { right, 1 - line }, t, White, Format("SOLAR WIND %.0f KM/S", m_WindNow), Hud::Align::Right);
	hud.Text(world, { right, 1 - line * 2 }, t, White, Format("STORM KP %.1f", m_KpNow), Hud::Align::Right);
	hud.Text(world, { right, 1 - line * 3 }, t, m_Package.WeatherLabel == "LIVE" ? Good : Warn, m_Package.WeatherLabel, Hud::Align::Right);

	// ---- Center: alarms, then timed messages ----
	y = 0.45f;
	if (life.ScrubberBroken)
	{
		hud.Text(world, { 0, y }, t * 1.4f, flash ? Bad : White, "SCRUBBER DOWN", Hud::Align::Center);
		y -= line * 1.4f;
	}
	if (IsBlackedOut(life))
	{
		hud.Text(world, { 0, 0 }, t * 2, Bad, "BLACKED OUT", Hud::Align::Center, 25);
		hud.Text(world, { 0, -line * 1.5f }, t, Dim, "THE TRUCK IS DRIFTING...", Hud::Align::Center, 25);
	}
	for (const Message& message : m_Messages)
	{
		hud.Text(world, { 0, y }, t, Faded(message.Color, std::min(message.TimeLeft, 1.0f)), message.Text, Hud::Align::Center);
		y -= line;
	}

	// ---- Prompts ----
	const float promptY = -0.55f;
	if (!m_Finished && !IsBlackedOut(life))
	{
		if (ticket.Active)
		{
			if (!ticket.Revealed)
				hud.Text(world, { 0, promptY }, t, Dim, "HOLD LEFT MOUSE AND DRAG TO SCRATCH. HANDS ARE OFF THE WHEEL!", Hud::Align::Center);
		}
		else if (seated)
		{
			hud.Text(world, { 0, promptY }, t, Dim, Format("MOUSE AIM  W THRUST  S BRAKE  [T] TICKET -%.1fKG%s", GetTicketCost(m_Settings), driver ? "  [E] STAND UP" : ""), Hud::Align::Center);
		}
		else if (driver)
		{
			if (CanReach(*driver, m_Wheel, m_Settings))
				hud.Text(world, { 0, promptY }, t, White, "[E] TAKE THE WHEEL", Hud::Align::Center);
			else if (CanReach(*driver, m_Scrubber, m_Settings))
				hud.Text(world, { 0, promptY }, t, life.ScrubberBroken ? Warn : Good,
					life.ScrubberBroken ? Format("HOLD [E] TO REPAIR THE SCRUBBER  %.0f%%", life.RepairProgress / m_Settings.ScrubberRepairTime * 100) : "AIR SCRUBBER: WORKING",
					Hud::Align::Center);
		}
		if (!truck.AtWheel && !ticket.Active)
		{
			const float drift = GetDriftAmount(truck, m_Settings);
			hud.Text(world, { 0, promptY - line }, t, drift > 0 ? Bad : Warn,
				drift > 0 ? Format("NOBODY AT THE WHEEL: DRIFTING %.0f%%", drift * 100) : Format("NOBODY AT THE WHEEL: HOLDING STEADY %.0fS", std::ceil(m_Settings.SteadyTime - truck.WheelEmptyTime)),
				Hud::Align::Center);
		}
	}

	// ---- Bottom: Route Tracker, always on ----
	const Vec3 toColony = m_Colony - truck.Position;
	const float progress = std::clamp(1 - toColony.Length() / m_Route.Length, 0.0f, 1.0f);
	const float barWidth = 1.4f;
	const float barY = -0.8f;
	hud.Box(world, { 0, barY }, { barWidth, t * 0.5f }, { 0.15f, 0.15f, 0.2f, 0.8f }, 10);
	hud.Box(world, { -barWidth * 0.5f + progress * barWidth * 0.5f, barY }, { progress * barWidth, t * 0.5f }, Good, 11);
	hud.Text(world, { -barWidth * 0.5f, barY + line * 0.8f }, t * 0.8f, Dim, "DEPOT");
	hud.Text(world, { barWidth * 0.5f, barY + line * 0.8f }, t * 0.8f, Dim, "COLONY", Hud::Align::Right);
	hud.Text(world, { 0, barY + line * 0.8f }, t * 0.8f, White, Format("%.0f%%", progress * 100), Hud::Align::Center);
	// On course: the truck's actual direction of travel, not its nose, against the way to the colony.
	const float offCourse = std::acos(std::clamp(Vec3::Dot(truck.Travel, toColony.Normalized()), -1.0f, 1.0f)) * RadToDeg;
	const Color courseColor = offCourse < 10 ? Good : offCourse < 35 ? Warn : Bad;
	hud.Text(world, { 0, barY - line }, t, courseColor, Format("OFF COURSE %.0f DEG    SPEED %.0f", offCourse, truck.Speed), Hud::Align::Center);
	const float gauge = std::min(offCourse / 90, 1.0f);
	hud.Box(world, { 0, barY - line * 1.8f }, { barWidth, t * 0.3f }, { 0.15f, 0.15f, 0.2f, 0.8f }, 10);
	hud.Box(world, { -barWidth * 0.5f + gauge * barWidth * 0.5f, barY - line * 1.8f }, { gauge * barWidth, t * 0.3f }, courseColor, 11);

	// Arrow toward the colony when it's off screen (seated, looking out of the truck).
	Vec2 screen;
	if (seated && !hud.ToScreen(m_Colony, screen))
	{
		const float length = std::sqrt(screen.X * screen.X + screen.Y * screen.Y);
		const Vec2 direction = length > 1e-6f ? screen * (1 / length) : Vec2{ 0, -1 };
		const float roll = std::atan2(direction.Y, direction.X) * RadToDeg;
		hud.Text(world, { direction.X * a * 0.8f - t, direction.Y * 0.7f }, t * 2.5f, Warn, ">", Hud::Align::Left, 20, roll);
	}

	if (ticket.Active)
		DrawTicket(world, ticket);
	if (m_Finished)
		DrawResults(world);
	hud.End(world);
}

void DriverSystem::DrawTicket(World& world, const Ticket& ticket)
{
	const float cell = 0.05f;
	const float width = cell * static_cast<float>(ticket.Columns);
	const float height = cell * static_cast<float>(ticket.Rows);
	const Vec2 center{ 0, 0.05f };
	const float left = center.X - width * 0.5f;
	const float top = center.Y + height * 0.5f;
	const float t = m_Settings.HudTextSize;

	m_Hud.Box(world, center, { width + 0.06f, height + 0.06f }, { 0.95f, 0.85f, 0.5f, 1 }, 30);
	m_Hud.Box(world, center, { width, height }, { 0.98f, 0.96f, 0.88f, 1 }, 31);
	m_Hud.Text(world, { center.X, top + 0.08f }, t * 1.2f, Warn, "SPACE SCRATCHER", Hud::Align::Center, 32);
	m_Hud.Text(world, center, t * 2, { 0.6f, 0.1f, 0.2f, 1 }, GetPrizeName(ticket.Won), Hud::Align::Center, 32);
	if (ticket.Revealed)
	{
		m_Hud.Text(world, { center.X, top - height - 0.08f }, t * 1.2f, Good, std::string("YOU WON: ") + GetPrizeName(ticket.Won), Hud::Align::Center, 32);
		return;
	}
	// The coating: every cell still covered is a grey square the coin scrapes away.
	for (int y = 0; y < ticket.Rows; ++y)
	{
		for (int x = 0; x < ticket.Columns; ++x)
		{
			if (ticket.Coating[static_cast<size_t>(y * ticket.Columns + x)])
				m_Hud.Box(world, { left + (static_cast<float>(x) + 0.5f) * cell, top - (static_cast<float>(y) + 0.5f) * cell }, { cell * 1.02f, cell * 1.02f },
					{ 0.55f, 0.57f, 0.6f, 1 }, 33);
		}
	}
	m_Hud.Text(world, { left + ticket.Coin.X * cell - t * 0.9f, top - ticket.Coin.Y * cell }, t * 1.8f, { 1, 0.8f, 0.2f, 1 }, "O", Hud::Align::Left, 34);
}

void DriverSystem::DrawResults(World& world)
{
	const float t = m_Settings.HudTextSize;
	m_Hud.Box(world, { 0, 0 }, { m_Hud.GetAspect() * 2 + 0.5f, 2.5f }, { 0, 0, 0.03f, 0.85f }, 50);
	float y = 0.6f;
	for (size_t i = 0; i < m_Results.size(); ++i)
	{
		const bool heading = i == 0;
		m_Hud.Text(world, { 0, y }, heading ? t * 1.8f : t * 0.9f, heading ? Warn : m_Results[i].rfind("  ", 0) == 0 ? Dim : White, m_Results[i], Hud::Align::Center, 60);
		y -= heading ? t * 3 : t * 1.4f;
	}
	m_Hud.Text(world, { 0, -0.9f }, t * 0.8f, Dim, "PRESS STOP, THEN PLAY TO RACE AGAIN", Hud::Align::Center, 60);
}
