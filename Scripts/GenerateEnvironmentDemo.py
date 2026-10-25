#!/usr/bin/env python3
"""Generate the original HDR panorama and environment lighting demo."""

import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CONTENT = ROOT / "Projects/Sample/Content"


def radiance(direction):
    x, y, z = direction
    if y >= 0:
        t = y ** 0.5
        color = [1.1 * (1 - t) + 0.12 * t,
                 0.9 * (1 - t) + 0.3 * t,
                 0.7 * (1 - t) + 0.85 * t]
        sun = max(0, x * -0.45 + y * 0.22 + z * -0.8655)
        glow = sun ** 512 * 35 + sun ** 24 * 1.5
        return [color[0] + glow, color[1] + glow * 0.8, color[2] + glow * 0.55]
    return [0.22 + 0.12 * (1 + y), 0.18 + 0.1 * (1 + y), 0.14 + 0.08 * (1 + y)]


def write_panorama():
    width, height = 256, 128
    data = bytearray(f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode())
    for y in range(height):
        theta = (y + 0.5) / height * math.pi
        for x in range(width):
            phi = ((x + 0.5) / width - 0.5) * 2 * math.pi
            color = radiance((math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi)))
            mantissa, exponent = math.frexp(max(color))
            scale = mantissa * 256 / max(color)
            data.extend([min(255, int(channel * scale)) for channel in color] + [exponent + 128])
    folder = CONTENT / "Environments"
    folder.mkdir(exist_ok=True)
    (folder / "Sunset.hdr").write_bytes(data)


def write_scene():
    entities = []

    def entity(name, position, components, rotation="0 0 0", scale="1 1 1"):
        number = len(entities) + 1
        entities.append(f'''entity e0000000-0000-4000-8000-{number:012d}
component Name
  Name "{name}"
component Transform
  Position {position}
  Rotation {rotation}
  Scale {scale}
{components}''')

    entity("Camera", "0 2.2 7", '''component Camera
  Perspective true
  FieldOfView 52
  Primary true
  Background 0.04 0.04 0.04 1
  Exposure 1
  Tonemap true
''', rotation="-10 0 0")
    entity("Skybox", "0 0 0", '''component Skybox
  Texture "Environments/Sunset.hdr"
  Intensity 1
  Rotation 0
  Visible true
  Enabled true
''')
    entity("Floor", "0 -0.05 0", '''component Mesh
  Mesh "Plane"
component Material
  Material "Materials/EnvironmentFloor.material"
''', scale="14 1 14")
    for name, x, material in [("Gold", -1.7, "Gold"), ("Plastic", 0, "Plastic"), ("Glass", 1.7, "Glass")]:
        entity(name, f"{x} 0.85 0", f'''component Mesh
  Mesh "Sphere"
component Material
  Material "Materials/{material}.material"
''', scale="1.7 1.7 1.7")
    (CONTENT / "Scenes/Environment.scene").write_text("# FirstPrinciples scene\nversion 2\n\n" + "\n".join(entities))
    (CONTENT / "Materials/EnvironmentFloor.material").write_text('''# FirstPrinciples material
Shader "Lit"
BaseColor 0.42 0.42 0.42 1
Roughness 0.85
Metallic 0
''')


if __name__ == "__main__":
    write_panorama()
    write_scene()
    print("Generated Sunset.hdr and Environment.scene")
