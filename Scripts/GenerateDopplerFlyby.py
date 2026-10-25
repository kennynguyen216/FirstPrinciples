"""Generate a 500-meter outdoor Doppler route. Requires Pillow for the signs."""

from pathlib import Path
import sys

sys.dont_write_bytecode = True
from GenerateAcousticsTest import entity, box, material, sign, entities


ROOT = Path(__file__).resolve().parents[1] / "Projects/Sample/Content"


if __name__ == "__main__":
    material("FlybyGround", "0.16 0.24 0.20")
    material("FlybyRoad", "0.10 0.12 0.16")
    material("FlybyStripe", "0.96 0.73 0.17", unlit=True)
    material("FlybyStart", "0.16 0.95 0.48", unlit=True)
    material("FlybyEnd", "1 0.30 0.15", unlit=True)
    box("Open terrain", (0, -0.5, 0), (1000, 1, 600), "FlybyGround")
    box("500 m flyby lane", (0, 0.025, 0), (550, 0.05, 14), "FlybyRoad")
    for x in range(-250, 251, 25):
        box(f"Lane marker {x:+d} m", (x, 0.07, 0), (7, 0.04, 0.35), "FlybyStripe")
        box(f"Distance post {x:+d} m", (x, 1.5, -9), (0.5, 3, 0.5), "FlybyStripe")
    for x, name, mat in [(-250, "Start -250 m", "FlybyStart"), (0, "Closest pass", "FlybyStripe"), (250, "Turnaround +250 m", "FlybyEnd")]:
        box(name, (x, 3, -12), (2, 6, 2), mat)
    # Player is created before its child camera so its stable ID is known.
    entity("Listener - WASD, Shift run, mouse look", (0, 1, 80), components='''component CharacterController
  Radius 0.35
  Height 1.8
  StepHeight 0.35
component PlayerInput
  WalkSpeed 12
  RunSpeed 25
  JumpSpeed 5
  LookSpeed 0.15
  Camera "Player Camera"
''')
    player_id = f"ac00571c-0000-4000-8000-{len(entities):012x}"
    entity("Player Camera", (0, 0.65, 0), components=f'''component Parent
  Parent {player_id}
component Camera
  Perspective true
  Primary true
  FieldOfView 100
  NearClip 0.05
  FarClip 1500
  Background 0.12 0.22 0.34 1
component AudioListener
  Enabled true
''')
    entity("Sun", rotation=(-55, -25, 0), components='''component DirectionalLight
  Color 1 0.94 0.84 1
  Intensity 1.3
  CastShadows true
  ShadowResolution 2048
  ShadowDistance 100
''')
    entity("Sky fill", components='''component AmbientLight
  Color 0.8 0.9 1 1
  Intensity 0.7
''')
    entity("Long Doppler Emitter - assign mono MP3", (0, 4, 0), (8, 8, 8), components='''component Mesh
  Mesh "Sphere"
component Material
  Material "Materials/Acoustics/DopplerEmitter.material"
component AudioSource
  Clip "Audio/PlaygroundDoppler.mp3"
  Enabled true
  Volume 0.6
  Pitch 1
  Loop true
  PlayOnStart false
  Bus "SFX"
  Spatial true
  Occlusion false
  DopplerScale 1
  MinDistance 80
  MaxDistance 750
component Scripts
  script "DopplerFlyby"
    HalfDistance 250
    Speed 45
    Center 0 4 0
''')
    sign("FlybyGuide", "500 M DOPPLER RUN", [
        "45 m/s / 11 seconds each way",
        "P Doppler on-off / F freeze / R restart",
        "WASD walk / Shift run / Mouse look",
        "Use a mono MP3 on Long Doppler Emitter"], "#80dfff", (0, 30, -20), 70)
    scene = "# FirstPrinciples scene\nversion 2\n\n" + "\n".join(entities)
    scene = scene.replace("ac00571c-", "f1b70000-")
    (ROOT / "Scenes/DopplerFlyby.scene").write_text(scene, encoding="utf-8")
    ids = [line.split()[1] for line in scene.splitlines() if line.startswith("entity ")]
    assert len(ids) == len(set(ids))
    assert scene.count("component Camera\n") == 1
    assert scene.count("component AudioListener\n") == 1
    assert scene.count("component AudioSource\n") == 1
    print("Generated DopplerFlyby: 500 m route, 45 m/s emitter, one player camera and listener.")
