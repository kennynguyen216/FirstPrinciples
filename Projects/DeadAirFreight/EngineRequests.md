# Engine requests from mk_ultra

Features this game needs from the engine. The game doesn't change the engine itself; each entry says
what the game does in the meantime.

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

## Portals (a camera rendered into a texture)

**Needed for:** seeing into the truck from outside. The cab and the bay are bigger than the hull,
so a window on the hull should show the rooms through a second camera, as a non-euclidean portal.

**Meanwhile:** from outside, `FreeCam` shows only the truck's model and hides the rooms behind it.
