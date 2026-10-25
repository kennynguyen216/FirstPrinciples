# Dead Air Freight

A 2v2 space trucking race where the cargo you haul is the air you breathe.

## Build and run

```powershell
Scripts\GenerateProjects.bat Projects\DeadAirFreight
msbuild Projects\DeadAirFreight\Build\DeadAirFreight.slnx /m /p:Configuration=Debug /p:Platform=x64
```

Run `msbuild` from the Developer PowerShell, and `Scripts\GetUltralight.ps1` once so the title screen
draws. The game opens on the Mission Kritikal Ultra title screen:

- **Host Game** starts a game others on the same network can join. Hosting alone plays solo.
- **Join Game** lists the games hosted on your network (hosts announce themselves on UDP port 7778),
  or takes a typed address.
- In the lobby the host presses **Start**. **Back** leaves the game for the title screen.

`Runtime --host 7777` and `Runtime --join <host ip>:7777` still work and skip the title screen.
Several copies can run from one folder for testing.

The countdown also starts on its own once `MatchConfig.PlayersRequired` players are in, and Enter
starts it early. Playing alone, press 1 to 4 in the lobby to pick your seat, so every role
can be tried on one machine.

| Seat | Crew | Role |
|---|---|---|
| P1 | A | Driver |
| P2 | A | Defender |
| P3 | B | Driver |
| P4 | B | Defender |

Seats are ordered by each peer's random network id, not by join order, so every peer works out the
same seating without sending anything. They lock when the countdown starts.

Either crewmate can fly or defend: where you stand is your role. An open hatch in each room's back
wall joins the cab and the cargo bay as if they were back to back: look through it to see the other
room, walk through it to be there. Press R at the cab's end to vent the bay through its airlock (the
creatures are dragged out with the air they stole). Seats only pick your crew and where you start.

In the bay: WASD walk, the mouse looks, the left button uses the laser at the speck in the middle of
the screen. Q swaps it between vaporize (red) and repair (green). Two vaporize hits
kill a Mite and put the oxygen it ate back in the tanks, and a shot that hits a tank instead punctures
it. Holding the repair beam on a tank patches one hole every 1.5 seconds.

Driver: the Driver's game from `game/driver-player`, unchanged, in `Source/Driver`. E sits at the
wheel or stands up, the mouse flies the truck, T buys a scratch ticket, and holding E at the scrubber
repairs it. Space weather (NOAA solar wind and Kp) and asteroid flybys (NASA NeoWs) are fetched live
at GO, falling back to the bundle in `Content/SpaceWeather` when offline. Its settings are in
`Content/Settings/Driver.settings`, its HUD is `Content/UI/DriverHud.html` drawn flat over that
player's own camera (it needs the Ultralight SDK, from `Scripts/GetUltralight`), and
`EngineRequests.md` lists what it still needs from the engine.

A life support readout for both crews goes to the log every few seconds, and Tab prints one.

## Networked play

The host runs the whole match: the truck, the bays, the creatures and the cargo. Everyone else sends
what they do (steering, sitting, shots, repairs, vents) and shows what the host sends back. The host
is whoever hosted the session (`--host`); everyone else joined it. One crewmate holds the wheel at a
time. Crewmates in the cab are placed in cab space, so they stay aboard the moving truck.
`Source/Net` holds the messages; the engine side is `NetworkSystem::SendGame` and `PollGame`.

For now the scene has one truck, Crew A's. Crew B needs the Driver's systems to run a second one, and
only the host sells scratch tickets.

## How the Driver and the rest fit together

`Source/Bridge/DriverBridge` joins them without changing the Driver's code:

- At GO a seated Driver gets the Driver's systems, so the lobby, seats and countdown still decide
  who drives. The live data fetch happens then, so GO can pause for a few seconds.
- The Driver's cargo number is the crew's oxygen tanks. Mites, leaks and missed shots show on the
  Driver's HUD, and the Driver's breathing and tickets empty the tanks the Defender guards.
- The Driver's cab breathes for the crew, and its colony clock and results end the match.

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

The engine's Jolt physics. Tanks and bay walls have static colliders, Mites kinematic spheres, and
the laser raycasts through `world.GetPhysics()`.

## Layout

| Path | What it holds |
|---|---|
| `Source/Match` | Match config and state, crew seating, Lobby -> Countdown -> Racing -> Results, delivery and score |
| `Source/LifeSupport` | Cargo tanks, crew resources, the colony clock and the log readout |
| `Source/Driver` | The Driver's game, kept exactly as written on `game/driver-player` |
| `Source/Bridge` | Joins the Driver's game to the match, cargo tanks and crews |
| `Source/Crew` | The Defender on foot: movement, look, and the laser's vaporize and repair modes |
| `Source/Audio` | Sound cues |
| `Source/Enemies` | The Siphon pool, spawning (`Siphons::Spawn` is what solar flare surges will call) and Mite behavior |
| `Content/Scenes/Main.scene` | The Driver's truck, cab, colony and camera, plus the cargo bays and the networked crew template. Tuning lives on its `Match` entity, real constants on `Real Data` |
| `Content/Data` | Real world data with sources. Every value is a placeholder marked `"verified": false` until checked |

## Status

- [x] M1: four players join, crews and roles assigned, trucks fly an open map, reaching the colony ends the match
- [x] M2: oxygen, water, CO2 and power, cab reserve and cargo, cargo tanks, colony clock, boost costs oxygen.
  Runs on each machine on its own until crew state is shared over the network.
- [x] M3: the Defender moves and shoots in the bay, Mites seek and drain tanks, kills return oxygen,
  missed shots puncture tanks, repair mode patches them
- [x] Merged with `game/driver-player`: the Driver's cab, flight, tickets, live space weather and
  asteroid flybys, sharing cargo with the bay
- [ ] Multiplayer for the Driver's truck and the shared cargo
