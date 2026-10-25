from pathlib import Path
import json
import math
import struct
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "Projects/Sample" if (ROOT / "Projects/Sample/Sample.fpproject").is_file() else ROOT
CONTENT = PROJECT / "Content"


def entity(name, components):
    identity = uuid.uuid5(uuid.NAMESPACE_URL, "firstprinciples/dia/" + name)
    lines = [f"entity {identity}"]
    for component, fields in components.items():
        lines.append("component " + component)
        lines.extend("  " + key + " " + value for key, value in fields.items())
    return "\n".join(lines)


def mesh(name, position, scale, material="White", rotation="0 0 0", shape="Cube"):
    return entity(name, {
        "Name": {"Name": json.dumps(name)},
        "Transform": {"Position": position, "Rotation": rotation, "Scale": scale},
        "Mesh": {"Mesh": json.dumps(shape)},
        "Material": {"Material": json.dumps("Materials/DIA/" + material + ".material")},
    })


def room(opening=False):
    walls = [
        mesh("Floor", "0 -2.1 0", "6.4 0.2 12"),
        mesh("Ceiling", "0 2.1 0", "6.4 0.2 12"),
        mesh("Left wall", "-3.1 0 0", "0.2 4 12", "Red"),
        mesh("Right wall", "3.1 0 0", "0.2 4 12", "Green"),
        mesh("Back wall", "0 0 -3.1", "6 4 0.2"),
    ]
    if not opening:
        walls.append(mesh("Front wall", "0 0 6.1", "6 4 0.2"))
    return walls


def header(sky=0, internal_height=180):
    return [entity("Camera", {
        "Name": {"Name": '"Main Camera"'},
        "Transform": {"Position": "0 0 5", "Rotation": "0 0 0", "Scale": "1 1 1"},
        "Camera": {"Perspective": "true", "FieldOfView": "55", "NearClip": "0.1", "FarClip": "128", "Exposure": "1", "Tonemap": "true"},
        "DIA": {"Enabled": "true", "SkyColor": "1 1 1 1", "SkyIntensity": str(sky),
                  "TraceDistance": "128", "DiffuseSamples": "4", "ReflectionSamples": "1",
                  "CacheUpdates": "4096", "InternalHeight": str(internal_height)},
    })]


def light(kind="PointLight", position="0 1 0", intensity=8, rotation="0 0 0"):
    fields = {"Color": "1 1 1 1", "Intensity": str(intensity)}
    if kind != "DirectionalLight":
        fields["Range"] = "30"
    if kind == "SpotLight":
        fields.update(InnerAngle="20", OuterAngle="30")
    return entity(kind, {
        "Name": {"Name": json.dumps(kind)},
        "Transform": {"Position": position, "Rotation": rotation, "Scale": "1 1 1"},
        kind: fields,
    })


def write_material(name, **fields):
    defaults = dict(Shader='"Lit"', BaseColor="0.75 0.75 0.75 1", Roughness="0.7", Metallic="0", TwoSided="false")
    defaults.update(fields)
    (CONTENT / "Materials/DIA" / (name + ".material")).write_text(
        "\n".join(key + " " + str(value) for key, value in defaults.items()) + "\n", encoding="utf-8")


def write_patch():
    width = 32
    pixels = b"".join(b"\0" + b"".join(bytes((255, 255, 255, 255) if 15 <= x <= 16 and 15 <= y <= 16 else (0, 0, 0, 255))
                       for x in range(width)) for y in range(width))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    output = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, width, 8, 6, 0, 0, 0))
    output += chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")
    (CONTENT / "Textures/DIAPatch.png").write_bytes(output)


def travel_room(index):
    result = []
    for block in room() + [light(), mesh("Receiver", "0 -0.5 -0.5", "1 2 1")]:
        lines = block.splitlines()
        name = next(line.split('"')[1] for line in lines if line.startswith("  Name "))
        lines[0] = f"entity {uuid.uuid5(uuid.NAMESPACE_URL, f'firstprinciples/dia/travel/{index}/{name}')}"
        for i, line in enumerate(lines):
            if line.startswith("  Name "):
                lines[i] = "  Name " + json.dumps(f"Room {index} {name}")
            if line.startswith("  Position "):
                x, y, z = map(float, line.split()[1:])
                lines[i] = f"  Position {x + index * 12:g} {y:g} {z:g}"
        result.append("\n".join(lines))
    return result


def main():
    for directory in ("Materials/DIA", "Scenes/DIA", "Textures"):
        (CONTENT / directory).mkdir(parents=True, exist_ok=True)
    write_material("White")
    write_material("Red", BaseColor="0.75 0.1 0.08 1")
    write_material("Green", BaseColor="0.08 0.65 0.12 1")
    write_material("Mirror", BaseColor="0.95 0.95 0.95 1", Metallic="1", Roughness="0.01")
    write_material("Emitter", BaseColor="0 0 0 1", EmissiveColor="1 0.92 0.8 1", EmissiveIntensity="12")
    write_material("Patch", BaseColor="0 0 0 1", EmissiveColor="1 1 1 1", EmissiveIntensity="100",
                   EmissiveMap='"Textures/DIAPatch.png"')
    write_material("Mixed", EmissiveColor="0.1 0.2 0.3 1", EmissiveIntensity="1")
    write_patch()
    fixtures = {}
    fixtures["Sealed"] = header(1) + room()
    fixtures["Open"] = header(1) + room(True)
    fixtures["Emissive"] = header() + room() + [mesh("Emitter panel", "0 1.85 -0.5", "2 0.1 2", "Emitter")]
    fixtures["Point"] = header() + room() + [light(), mesh("Receiver", "0 -0.5 -0.5", "1 2 1")]
    fixtures["Spot"] = header() + room() + [light("SpotLight", "0 1 2", 20), mesh("Receiver", "0 -0.5 -1", "1 2 1")]
    fixtures["BlockerBefore"] = header() + room() + [light(position="0 0 1"), mesh("Blocker", "0 0 0", "1 1 0.05")]
    fixtures["BlockerBehind"] = header() + room() + [light(position="0 0 1"), mesh("Blocker", "0 0 2", "1 1 0.05")]
    fixtures["ThinWall"] = header() + room() + [light(position="-1 0 0"), mesh("Thin wall", "0 0 0", "0.02 4 6")]
    fixtures["Mirror"] = header() + room() + [mesh("Mirror", "0 0 -2.9", "4 3 0.02", "Mirror"),
        mesh("Offscreen emitter", "0 1 5.5", "1 1 0.1", "Emitter")]
    fixtures["BrightPatch"] = header() + room() + [mesh("Textured emitter", "0 1.85 -0.5", "2 0.1 2", "Patch")]
    sweep = []
    for metallic in (0, 1):
        for index, roughness in enumerate((0.01, 0.2, 0.5, 0.9)):
            name = f"M{metallic}R{index}"
            write_material(name, Metallic=str(metallic), Roughness=str(roughness), BaseColor="0.9 0.55 0.2 1")
            sweep.append(mesh(name, f"{(index - 1.5) * 1.1} {metallic - 0.5} -0.5", "0.8 0.8 0.8", name))
    fixtures["Sweep"] = header(0.1) + room(True) + sweep + [light()]
    fixtures["Review"] = header(0.02) + room() + [light(position="-1 1.5 1", intensity=10),
        mesh("White box", "-1.3 -1.3 -0.4", "1 1.4 1", rotation="0 25 0"),
        mesh("Metal sphere", "1 -1 -0.5", "1.6 1.6 1.6", "M1R1", shape="Sphere"),
        mesh("Mirror", "0 0 -2.9", "2.3 2.5 0.03", "Mirror"),
        mesh("Offscreen emitter", "0 1 5.5", "1 1 0.1", "Emitter"),
        mesh("Ceiling emitter", "0 1.85 -0.5", "1.2 0.1 1.2", "Emitter")]
    fixtures["Travel"] = header() + [block for index in range(4) for block in travel_room(index)]
    fixtures["Mixed"] = header() + room() + [entity("Mixed surfaces", {
        "Name": {"Name": '"Mixed surfaces"'},
        "Transform": {"Position": "0 0 0", "Scale": "0.6 0.6 0.6"},
        "Mesh": {"Mesh": '"Cube"'}, "Sprite": {"Size": "2 2", "Tint": "1 1 1 1", "Layer": "0"},
        "Material": {"Material": '"Materials/DIA/Mixed.material"'},
    })]
    manifest = {"baseline": "75e5fb7", "seed": 911, "output_extent": [1280, 720],
                "transport_extent": [320, 180], "reference_extent": [64, 64], "reference_seeds": [911, 1237, 3217],
                "diffuse_samples": 4, "reflection_samples": 1, "quality_extent": [64, 64], "primary_sampling": "pixel-center",
                "warmup_frames": 300, "measured_frames": 300, "pairs": 3, "fixtures": {}}
    for name, entities in fixtures.items():
        identities = [entry.splitlines()[0] for entry in entities]
        assert len(identities) == len(set(identities)), name
        text = "version 2\n\n" + "\n\n".join(entities) + "\n"
        path = CONTENT / "Scenes/DIA" / (name + ".scene")
        path.write_text(text, encoding="utf-8")
        manifest["fixtures"][name] = {"scene": path.relative_to(CONTENT).as_posix(),
            "receiver_region": [0.1, 0.55, 0.9, 0.9], "black_floor": 1e-5}
    (ROOT / "docs/DIAFixtures.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    assert math.isfinite(manifest["fixtures"]["Sealed"]["black_floor"])
    print(f"Generated {len(fixtures)} deterministic DIA fixtures")


if __name__ == "__main__":
    main()
