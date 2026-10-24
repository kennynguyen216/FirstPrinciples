# Dead Air Freight

A 2v2 space trucking race where the cargo you haul is the air you breathe.

## Build and run

```powershell
Scripts\GenerateProjects.bat Projects\DeadAirFreight
msbuild Projects\DeadAirFreight\Build\DeadAirFreight.slnx /m /p:Configuration=Debug /p:Platform=x64
```

Run `msbuild` from the Developer PowerShell. Then start one host and three joiners, each on its own machine:

```powershell
Projects\DeadAirFreight\Binaries\Debug\Runtime --host 7777
Projects\DeadAirFreight\Binaries\Debug\Runtime --join <host ip>:7777
```

The countdown starts once `MatchConfig.PlayersRequired` players are in. Press Enter to start early
when testing with fewer.

| Seat | Crew | Role |
|---|---|---|
| P1 | A | Driver |
| P2 | A | Defender |
| P3 | B | Driver |
| P4 | B | Defender |

Seats are ordered by each peer's random network id, not by join order, so every peer works out the
same seating without sending anything. They lock when the countdown starts.

Driver: W/S thrust, A/D turn, Shift boost (vents cargo oxygen). Reaching the colony docks the truck
and hands its cargo over. The match ends when every truck has docked, or when the colony's oxygen
runs out and everybody loses. Until there is a HUD, match events and a life support readout go to
the log every few seconds, and Tab prints one on demand.

## How the resources work

- Each crew breathes, drinks and exhales at NASA's per person rates (the `Real Data` entity),
  sped up by `MatchConfig.SecondsPerRealDay` so the ratios stay true.
- The cab reserve goes first. After that the crew breathes the cargo, which is less to deliver.
- Boosting vents cargo oxygen, more for every leaking tank.
- A holed oxygen tank leaks by choked gas flow through the hole, so it loses a fixed share of what is
  left each second. The crew breathes from the emptiest tank first, which is usually the leaking one.
- The scrubber removes CO2 and the recycler wins back water while the battery has charge. Their
  health fields are where repairs (M4) and solar flares (M6) will plug in.
- The colony clock is the colony's oxygen divided by its crew's use. Deliveries extend it.
- Score is kilograms delivered plus a bonus per second under par.

## Physics

`Source/Physics` holds collider and rigid body components plus `Raycast` and `OverlapSphere`. For now
`PhysicsQueries.cpp` answers them by testing every collider. When Jolt lands, answer them from Jolt in
that one file and gameplay code stays the same. If the engine brings its own collider components,
move the fields over and delete ours.

## Layout

| Path | What it holds |
|---|---|
| `Source/Match` | Match config and state, crew seating, Lobby -> Countdown -> Racing -> Results, delivery and score |
| `Source/LifeSupport` | Cargo tanks, crew resources, the colony clock and the log readout |
| `Source/Truck` | Truck handling, boost and the chase camera |
| `Source/Physics` | Colliders, bodies and the queries Jolt will answer |
| `Content/Scenes/Main.scene` | Depot, colony, asteroids, cargo bay spots and the networked crew template. Tuning lives on its `Match` entity, real constants on `Real Data` |
| `Content/Data` | Real world data with sources. Every value is a placeholder marked `"verified": false` until checked |

## Status

- [x] M1: four players join, crews and roles assigned, trucks fly an open map, reaching the colony ends the match
- [x] M2: oxygen, water, CO2 and power, cab reserve and cargo, cargo tanks, colony clock, boost costs oxygen.
  Runs on each machine on its own until crew state is shared over the network.
