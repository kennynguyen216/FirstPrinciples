# Engine requests from mk_ultra

Features this game needs from the engine. The game doesn't change the engine itself; each entry says
what the game does in the meantime.

## Text and 2D overlay drawing

**Needed for:** the whole Driver HUD, the scratch ticket and the results screen.

**Ask:** game code drawing text, filled rectangles and textured quads in screen space on top of the
scene, and reading the view size in pixels.

**Coming:** a Blazor-style C++ UI framework. When it lands, delete `Source/Hud.h/.cpp`, `Content/UI/`
and `Tools/MakeFont.py`, and rebuild the HUD, ticket and results screens with it (search the code for
`TODO(engine UI)`).

**Meanwhile:** `Source/Hud.cpp` places sprites `HudDistance` in front of the camera. Text is one
sprite per character with a glyph material (`Tools/MakeFont.py` makes the font and materials).
Without the view size the HUD lays out for `HudAspect` (16:10); narrower windows crop the sides.
Costs one entity per visible character.

## Screen blur (post-processing)

**Needed for:** rising CO2 "slowly blurs and darkens" the Driver's view.

**Meanwhile:** CO2 only darkens (a translucent black sprite) and strengthens the wobble.

## HTTP requests

**Needed for:** fetching live NOAA and NASA data at match start.

**Meanwhile:** `Source/WeatherFetch.cpp` runs `curl` through `popen` (curl ships with macOS and
Windows 10+). If curl is missing or offline, the saved fallback bundle plays instead.

## Parenting (child transforms)

**Needed for:** a cab that rides inside the truck, so the windshield shows the truck's real view.

**Meanwhile:** the cab is laid out at y=-5000 in the scene, and every frame `DriverSystem` moves each
cab part (names starting "Cab", plus "Wheel" and "Scrubber") to the truck's position and rotation.
Driver walking and reach checks stay in that layout. Anything added to the cab needs one of those names.
