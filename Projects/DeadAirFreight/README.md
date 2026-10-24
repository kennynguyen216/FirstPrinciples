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
when testing with fewer. Playing alone, press 1 to 4 in the lobby to pick your seat, so every role
can be tried on one machine.

| Seat | Crew | Role |
|---|---|---|
| P1 | A | Driver |
| P2 | A | Defender |
| P3 | B | Driver |
| P4 | B | Defender |

Seats are ordered by each peer's random network id, not by join order, so every peer works out the
same seating without sending anything. They lock when the countdown starts.

Defender: WASD walk, hold the right mouse button to look, left button uses the laser at the speck in
the middle of the screen. R swaps it between vaporize (cyan) and repair (amber). Two vaporize hits
kill a Mite and put the oxygen it ate back in the tanks, and a shot that hits a tank instead punctures
it. Holding the repair beam on a tank patches one hole every 1.5 seconds.

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
- Mites get into both bays at the same moments and spots, fly to the nearest oxygen tank, latch on and
  drain it into their bellies, swelling as they feed. One latched for `MiteBiteHoleSeconds` chews a hole.
  They come from a fixed pool per crew, so nothing is created or destroyed mid match. They perch on
  tank rims, and tanks stand just under eye height, so a Mite shows over the rows.

## Sound

Each game event has a cue on the Match entity's `SoundCues` component: a WAV under `Content`, or empty
for silence. `MiteChitter` loops on every awake Mite, positioned, so the Defender can hear where they
are. `LaserFire`, `MiteChitter` and `Countdown` point at placeholder clips in `Content/Audio` for now;
swap in the real ones as they are made. `Sounds::Play` and `Sounds::PlayAt` play a cue from code.

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
| `Source/Crew` | The Defender on foot: movement, look, and the laser's vaporize and repair modes |
| `Source/Audio` | Sound cues |
| `Source/Enemies` | The Siphon pool, spawning (`Siphons::Spawn` is what solar flare surges will call) and Mite behavior |
| `Source/Physics` | Colliders, bodies and the queries Jolt will answer |
| `Content/Scenes/Main.scene` | Depot, colony, asteroids, cargo bay spots and the networked crew template. Tuning lives on its `Match` entity, real constants on `Real Data` |
| `Content/Data` | Real world data with sources. Every value is a placeholder marked `"verified": false` until checked |

## Status

- [x] M1: four players join, crews and roles assigned, trucks fly an open map, reaching the colony ends the match
- [x] M2: oxygen, water, CO2 and power, cab reserve and cargo, cargo tanks, colony clock, boost costs oxygen.
  Runs on each machine on its own until crew state is shared over the network.
- [x] M3: the Defender moves and shoots in the bay, Mites seek and drain tanks, kills return oxygen,
  missed shots puncture tanks, repair mode patches them
- M4 (cab and helm) is being built separately with the driving code
