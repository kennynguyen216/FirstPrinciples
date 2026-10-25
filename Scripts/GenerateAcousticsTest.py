"""Rebuild the original 3D acoustics test assets. Requires Pillow for the signs."""

from pathlib import Path
import math
import random
import struct
import wave

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1] / "Projects/Sample/Content"
entities = []


def entity(name, position=(0, 0, 0), scale=(1, 1, 1), rotation=(0, 0, 0), components=""):
    index = len(entities) + 1
    def vector(values):
        return " ".join(f"{v:g}" for v in values)
    entities.append(f'''entity ac00571c-0000-4000-8000-{index:012x}
component Name
  Name "{name}"
component Transform
  Position {vector(position)}
  Rotation {vector(rotation)}
  Scale {vector(scale)}
{components}''')


def material(name, color, texture="", unlit=False, sound=(0.25, 0.4, 0.5)):
    path = ROOT / f"Materials/Acoustics/{name}.material"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f'''# FirstPrinciples material
Shader "{'Unlit' if unlit else 'Lit'}"
BaseColor {color} 1
BaseColorMap "{texture}"
Metallic 0
Roughness 0.8
SoundTransmission {sound[0]}
SoundAbsorption {sound[1]}
SoundDamping {sound[2]}
''', encoding="utf-8")


def box(name, position, scale, mat):
    entity(name, position, scale, components=f'''component Mesh
  Mesh "Cube"
component Material
  Material "Materials/Acoustics/{mat}.material"
component Collider
  Shape "Box"
  Size 1 1 1
  Trigger false
''')


def sign(name, heading, lines, accent, position, width=4):
    image = Image.new("RGB", (1200, 600), "#101b29")
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 16, 600), fill=accent)
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    def font(size):
        return ImageFont.truetype(str(font_path), size) if font_path.exists() else ImageFont.load_default(size=size)
    draw.text((60, 35), "FIRST PRINCIPLES / ACOUSTICS LAB", font=font(26), fill="#96a9bf")
    draw.text((60, 105), heading, font=font(66), fill=accent)
    for i, line in enumerate(lines):
        draw.text((60, 218 + i * 61), line, font=font(34), fill="#eaf1fa")
    path = ROOT / f"Textures/Acoustics/{name}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    material(name, "1 1 1", f"Textures/Acoustics/{name}.png", unlit=True)
    entity(heading, position, (width, width / 2, 1), components=f'''component Mesh
  Mesh "Quad"
component Material
  Material "Materials/Acoustics/{name}.material"
''')


if __name__ == "__main__":
    material("Outside", "0.21 0.32 0.24")
    entity("Listener - 1/2/3/4 rooms, Space clap, R dry, T auto, WASD + mouse", (-18, 1.65, 3), components='''component Camera
  Perspective true
  Primary true
  FieldOfView 70
  NearClip 0.05
  FarClip 180
  Background 0.10 0.16 0.24 1
component AudioListener
  Enabled true
component AcousticsTest
  AutoClap true
''')
    entity("Daylight", rotation=(-55, -25, 0), components='''component DirectionalLight
  Color 1 0.94 0.84 1
  Intensity 1.4
  CastShadows true
  ShadowResolution 2048
  ShadowDistance 100
''')
    entity("Room fill", components='''component AmbientLight
  Color 0.80 0.88 1 1
  Intensity 0.7
''')
    box("Outdoor promenade", (12, -0.5, 7), (92, 0.5, 42), "Outside")
    rooms = [(-18, 10, 4, 12, "Soft", 0.35, 0.9, "#59dec4", "01 / SOFT ROOM"),
             (0, 12, 5, 14, "Bright", 1.6, 0.15, "#80c9ff", "02 / BRIGHT ROOM"),
             (24, 22, 10, 24, "Hall", 3.4, 0.25, "#ffce80", "03 / LARGE HALL")]
    for x, width, height, depth, mat, decay, damping, accent, title in rooms:
        sound = (0.05, 0.85, damping) if mat == "Soft" else (0.25, 0.12 if mat == "Bright" else 0.2, damping)
        colors = {"Soft": "0.08 0.39 0.34", "Bright": "0.29 0.52 0.67", "Hall": "0.53 0.34 0.16"}
        material(mat, colors[mat], sound=sound)
        material(mat + "Wall", "0.32 0.39 0.48", sound=sound)
        material(mat + "Ceiling", "0.20 0.25 0.32", sound=sound)
        entity(f"{mat} room acoustics", (x, height / 2, 0), components=f'''component AudioRoom
  Enabled true
  Size {width} {height} {depth}
  Wet 0.85
  Decay {decay}
''')
        box(f"{mat} floor", (x, -0.2, 0), (width, 0.4, depth), mat)
        box(f"{mat} ceiling", (x, height + 0.2, 0), (width + 0.8, 0.4, depth + 0.8), mat + "Ceiling")
        for side in (-1, 1):
            box(f"{mat} side wall", (x + side * (width / 2 + 0.2), height / 2, 0), (0.4, height, depth), mat + "Wall")
            segment = (width - 3) / 2
            box(f"{mat} doorway jamb", (x + side * (1.5 + segment / 2), height / 2, depth / 2 + 0.2), (segment, height, 0.4), mat + "Wall")
        box(f"{mat} back wall", (x, height / 2, -depth / 2 - 0.2), (width + 0.8, height, 0.4), mat + "Wall")
        box(f"{mat} door lintel", (x, 3 + (height - 3) / 2, depth / 2 + 0.2), (3, height - 3, 0.4), mat)
        sign(mat + "Sign", title, [f"Max decay {decay:g}s / absorption {sound[1]:g}",
             "SPACE clap    R dry / wet    B overlap test", "1 soft    2 bright    3 hall    4 outside",
             "WASD walk    Mouse look    T auto-clap"], accent, (x, 2, -depth / 2 + 0.03), min(width - 1, 7))
    box("Outdoor listening pad", (48, -0.1, 0), (14, 0.2, 14), "Outside")
    sign("OutsideSign", "04 / OPEN AIR", ["No enclosed-room reflections or reverb",
         "Compare the same clap with each room", "1 soft    2 bright    3 hall    4 outside",
         "SPACE clap    B overlap test    T auto-clap"], "#a8e4b1", (48, 2.2, -5), 6)
    material("ClapEmitter", "1 0.65 0.12", unlit=True)
    material("ClapStand", "0.09 0.11 0.15")
    for x, name in [(-18, "Soft"), (0, "Bright"), (24, "Hall"), (48, "Outdoor")]:
        box(f"{name} clap pedestal", (x, 0.6, -1), (0.8, 1.2, 0.8), "ClapStand")
        entity(f"{name} clap emitter", (x, 1.5, -1), (0.65, 0.65, 0.65), components='''component Mesh
  Mesh "Sphere"
component Material
  Material "Materials/Acoustics/ClapEmitter.material"
component Collider
  Shape "Sphere"
  Radius 0.5
  Trigger false
component AudioSource
  Clip "Audio/Clap.wav"
  Enabled true
  Volume 0.9
  Pitch 1
  Loop false
  PlayOnStart false
  Bus "SFX"
  Spatial true
  Occlusion true
  MinDistance 3
  MaxDistance 30
  Priority 128
''')
        sign("ClapSourceSign", "CLAP SOURCE", ["Walk around this object",
             "SPACE clap / T auto-clap", "B overlap test"], "#ffce80", (x, 2.8, -1), 2.4)
    material("DopplerEmitter", "0.1 0.75 1", unlit=True)
    doppler_clip = "Misery.mp3" if (ROOT / "Audio/Misery.mp3").is_file() else "DopplerTone.wav"
    entity("Moving Doppler emitter", (12, 1.7, 18), (1.2, 1.2, 1.2), components=f'''component Mesh
  Mesh "Sphere"
component Material
  Material "Materials/Acoustics/DopplerEmitter.material"
component MovingAudio
  Distance 40
  Speed 30
component AudioSource
  Clip "Audio/{doppler_clip}"
  Enabled false
  Volume 0.55
  Pitch 1
  Loop true
  PlayOnStart false
  Bus "SFX"
  Spatial true
  Occlusion true
  DopplerScale 1
  MinDistance 10
  MaxDistance 80
  Priority 128
''')
    sign("DopplerSign", "DOPPLER / PRESS 5", ["Cyan sphere: 80m outdoor flyby", "P compares Doppler ON / OFF",
         "Walk closer to hear the passing shift", "1 - 4 returns to clap rooms"], "#80dfff", (12, 3, 17.9), 5)
    (ROOT / "Scenes/Acoustics3D.scene").write_text("# FirstPrinciples scene\nversion 2\n\n" + "\n".join(entities), encoding="utf-8")
    rng = random.Random(2718)
    rate = 48000
    samples = []
    for i in range(int(rate * 0.13)):
        t = i / rate
        envelope = min(1, t / 0.0007) * math.exp(-65 * t)
        value = rng.uniform(-1, 1) * envelope * 0.65
        samples.append(round(value * 32767))
    with wave.open(str(ROOT / "Audio/Clap.wav"), "wb") as output:
        output.setparams((1, 2, rate, len(samples), "NONE", "not compressed"))
        output.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    tone = [round((0.18 * math.sin(2 * math.pi * 660 * i / rate)
                   + 0.05 * math.sin(2 * math.pi * 1320 * i / rate)) * 32767) for i in range(rate)]
    with wave.open(str(ROOT / "Audio/DopplerTone.wav"), "wb") as output:
        output.setparams((1, 2, rate, len(tone), "NONE", "not compressed"))
        output.writeframes(struct.pack(f"<{len(tone)}h", *tone))
    print(f"Generated {len(entities)} scene entities, signs, materials and original mono clap.")
