#!/usr/bin/env python3
# Writes Projects/Sample/Content/Characters/Noodle.gltf, a small skinned character for the sample.
# A soft tube on five stacked bones with two eyes, plus Idle, Walk and Jump clips.
# Usage: python3 Scripts/GenerateSampleCharacter.py
import base64
import json
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Projects", "Sample", "Content", "Characters", "Noodle.gltf")

BONES = ["Hips", "Spine", "Chest", "Neck", "Head"]
BONE_HEIGHTS = [0.0, 0.35, 0.7, 1.05, 1.4]
HEIGHT = 1.7
SEGMENTS = 16
RINGS = 24


def radius_at(y):
    # Wide at the bottom, narrower neck, round head.
    t = y / HEIGHT
    r = 0.34 - 0.08 * t + 0.05 * math.sin(t * math.pi * 2.2)
    if y > 1.4:
        x = (y - 1.4) / (HEIGHT - 1.4)
        r *= math.sqrt(max(0.0, 1 - x * x))
    return max(r, 0.0)


def bone_weights(y):
    # Blend between the two nearest bones.
    for i in range(len(BONE_HEIGHTS) - 1):
        a, b = BONE_HEIGHTS[i], BONE_HEIGHTS[i + 1]
        if y <= b:
            t = max(0.0, (y - a) / (b - a))
            return (i, i + 1), (1 - t, t)
    return (len(BONES) - 1, 0), (1.0, 0.0)


def build_mesh():
    body = {"pos": [], "nrm": [], "uv": [], "joints": [], "weights": [], "idx": []}
    # Side rings, with a dome closing the top and a flat bottom.
    rings = []
    for r in range(RINGS + 1):
        y = HEIGHT * r / RINGS
        rings.append((y, radius_at(y)))
    for r, (y, rad) in enumerate(rings):
        for s in range(SEGMENTS + 1):
            a = 2 * math.pi * s / SEGMENTS
            x, z = math.sin(a) * rad, math.cos(a) * rad
            body["pos"].append((x, y, z))
            # Lean the normal up or down where the radius changes, so the dome shades round.
            slope = (radius_at(min(HEIGHT, y + 0.01)) - radius_at(max(0.0, y - 0.01))) / 0.02
            n = (math.sin(a), -slope, math.cos(a))
            length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
            body["nrm"].append((n[0] / length, n[1] / length, n[2] / length))
            body["uv"].append((s / SEGMENTS, 1 - r / RINGS))
            j, w = bone_weights(y)
            body["joints"].append((j[0], j[1], 0, 0))
            body["weights"].append((w[0], w[1], 0, 0))
    row = SEGMENTS + 1
    for r in range(RINGS):
        for s in range(SEGMENTS):
            a, b = r * row + s, r * row + s + 1
            c, d = (r + 1) * row + s, (r + 1) * row + s + 1
            body["idx"] += [a, b, d, a, d, c]
    # Top and bottom caps.
    for top in (True, False):
        y, rad = rings[-1] if top else rings[0]
        center = len(body["pos"])
        body["pos"].append((0.0, y, 0.0))
        body["nrm"].append((0.0, 1.0 if top else -1.0, 0.0))
        body["uv"].append((0.5, 0.5))
        j, w = bone_weights(y)
        body["joints"].append((j[0], j[1], 0, 0))
        body["weights"].append((w[0], w[1], 0, 0))
        start = len(body["pos"])
        for s in range(SEGMENTS + 1):
            a = 2 * math.pi * s / SEGMENTS
            body["pos"].append((math.sin(a) * rad, y, math.cos(a) * rad))
            body["nrm"].append((0.0, 1.0 if top else -1.0, 0.0))
            body["uv"].append((0.5 + math.sin(a) * 0.5, 0.5 + math.cos(a) * 0.5))
            body["joints"].append((j[0], j[1], 0, 0))
            body["weights"].append((w[0], w[1], 0, 0))
        for s in range(SEGMENTS):
            if top:
                body["idx"] += [center, start + s, start + s + 1]
            else:
                body["idx"] += [center, start + s + 1, start + s]

    # Two boxy eyes on the head, facing +Z.
    eyes = {"pos": [], "nrm": [], "uv": [], "joints": [], "weights": [], "idx": []}
    head = BONES.index("Head")
    for side in (-1, 1):
        cx, cy = side * 0.11, 1.38
        cz = radius_at(cy) - 0.02
        lo = (cx - 0.055, cy - 0.07, cz - 0.03)
        hi = (cx + 0.055, cy + 0.07, cz + 0.05)
        add_box(eyes, lo, hi, head)
    return body, eyes


def add_box(part, lo, hi, bone):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    faces = [
        ([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0)),
        ([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0)),
        ([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0)),
        ([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0)),
        ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1)),
        ([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1)),
    ]
    for corners, normal in faces:
        base = len(part["pos"])
        for i, c in enumerate(corners):
            part["pos"].append(c)
            part["nrm"].append(normal)
            part["uv"].append(((0, 1, 1, 0)[i], (1, 1, 0, 0)[i]))
            part["joints"].append((bone, 0, 0, 0))
            part["weights"].append((1.0, 0.0, 0.0, 0.0))
        part["idx"] += [base, base + 1, base + 2, base, base + 2, base + 3]


def quat_axis_angle(axis, degrees):
    half = math.radians(degrees) / 2
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def euler(x=0.0, z=0.0):
    return quat_mul(quat_axis_angle((1, 0, 0), x), quat_axis_angle((0, 0, 1), z))


def clips():
    out = {}
    keys = 17

    # Idle: slow sway and a little breathing.
    def idle(t):
        p = 2 * math.pi * t / 2.0
        rot = {b: euler(z=3 * math.sin(p - i * 0.6)) for i, b in enumerate(BONES[1:])}
        rot["Head"] = euler(x=4 * math.sin(p * 2), z=-3 * math.sin(p))
        return {"Hips": (0, 0.02 * math.sin(p * 2), 0)}, rot, {}
    out["Idle"] = (2.0, idle)

    # Walk: bounce twice per cycle and wiggle from side to side.
    def walk(t):
        p = 2 * math.pi * t / 0.8
        rot = {b: euler(x=6, z=10 * math.sin(p - i * 0.9)) for i, b in enumerate(BONES[1:])}
        rot["Hips"] = euler(z=6 * math.sin(p))
        return {"Hips": (0, 0.08 * abs(math.sin(p)), 0)}, rot, {}
    out["Walk"] = (0.8, walk)

    # Jump: crouch, leap, land and settle.
    def jump(t):
        def ease(a, b, x):
            x = max(0.0, min(1.0, x))
            x = x * x * (3 - 2 * x)
            return a + (b - a) * x
        if t < 0.15:
            squash, height, bend = ease(1, 0.75, t / 0.15), 0, ease(0, 12, t / 0.15)
        elif t < 0.5:
            x = (t - 0.15) / 0.35
            squash, height, bend = ease(0.75, 1.1, x * 3), math.sin(x * math.pi / 2) * 0.9, ease(12, -8, x)
        elif t < 0.8:
            x = (t - 0.5) / 0.3
            squash, height, bend = 1.1, math.cos(x * math.pi / 2) * 0.9, ease(-8, 0, x)
        else:
            x = (t - 0.8) / 0.2
            squash, height, bend = 1 - 0.2 * math.sin(x * math.pi), 0, 6 * math.sin(x * math.pi)
        rot = {b: euler(x=bend) for b in BONES[1:]}
        return {"Hips": (0, height, 0)}, rot, {"Hips": (1 / math.sqrt(squash), squash, 1 / math.sqrt(squash))}
    out["Jump"] = (1.0, jump)

    sampled = {}
    for name, (duration, fn) in out.items():
        times = [duration * k / (keys - 1) for k in range(keys)]
        frames = [fn(t) for t in times]
        sampled[name] = (times, frames)
    return sampled


class Writer:
    def __init__(self):
        self.buffer = bytearray()
        self.views = []
        self.accessors = []

    def add(self, values, fmt, kind, component, with_bounds=False):
        while len(self.buffer) % 4:
            self.buffer.append(0)
        offset = len(self.buffer)
        for v in values:
            self.buffer += struct.pack(fmt, *(v if isinstance(v, (tuple, list)) else (v,)))
        self.views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(self.buffer) - offset})
        accessor = {"bufferView": len(self.views) - 1, "componentType": component, "count": len(values), "type": kind}
        if with_bounds:
            n = len(values[0]) if isinstance(values[0], (tuple, list)) else 1
            column = (lambda v, i: v[i]) if n > 1 else (lambda v, i: v)
            accessor["min"] = [min(column(v, i) for v in values) for i in range(n)]
            accessor["max"] = [max(column(v, i) for v in values) for i in range(n)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1


def main():
    w = Writer()
    body, eyes = build_mesh()
    primitives = []
    for material, part in enumerate((body, eyes)):
        primitives.append({
            "attributes": {
                "POSITION": w.add(part["pos"], "<3f", "VEC3", 5126, True),
                "NORMAL": w.add(part["nrm"], "<3f", "VEC3", 5126),
                "TEXCOORD_0": w.add(part["uv"], "<2f", "VEC2", 5126),
                "JOINTS_0": w.add(part["joints"], "<4H", "VEC4", 5123),
                "WEIGHTS_0": w.add(part["weights"], "<4f", "VEC4", 5126),
            },
            "indices": w.add(part["idx"], "<I", "SCALAR", 5125),
            "material": material,
        })

    # Bones are a chain, each one placed relative to its parent.
    nodes = []
    for i, name in enumerate(BONES):
        node = {"name": name, "translation": [0, BONE_HEIGHTS[i] - (BONE_HEIGHTS[i - 1] if i else 0), 0]}
        if i + 1 < len(BONES):
            node["children"] = [i + 1]
        nodes.append(node)
    nodes.append({"name": "Noodle", "mesh": 0, "skin": 0})
    inverse_bind = []
    for h in BONE_HEIGHTS:
        inverse_bind.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -h, 0, 1))

    animations = []
    for name, (times, frames) in clips().items():
        time_accessor = w.add(times, "<f", "SCALAR", 5126, True)
        samplers, channels = [], []
        for path, slot, fmt, kind in (("translation", 0, "<3f", "VEC3"), ("rotation", 1, "<4f", "VEC4"), ("scale", 2, "<3f", "VEC3")):
            for bone in BONES:
                if bone not in frames[0][slot]:
                    continue
                values = [f[slot][bone] for f in frames]
                if path == "translation":
                    rest = BONE_HEIGHTS[BONES.index(bone)] - (BONE_HEIGHTS[BONES.index(bone) - 1] if BONES.index(bone) else 0)
                    values = [(v[0], v[1] + rest, v[2]) for v in values]
                samplers.append({"input": time_accessor, "output": w.add(values, fmt, kind, 5126)})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": BONES.index(bone), "path": path}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})

    gltf = {
        "asset": {"version": "2.0", "generator": "FirstPrinciples GenerateSampleCharacter.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, len(BONES)]}],
        "nodes": nodes,
        "skins": [{"joints": list(range(len(BONES))), "inverseBindMatrices": w.add(inverse_bind, "<16f", "MAT4", 5126), "skeleton": 0}],
        "materials": [{"name": "Body"}, {"name": "Eyes"}],
        "meshes": [{"name": "Noodle", "primitives": primitives}],
        "animations": animations,
        "buffers": [{"byteLength": len(w.buffer), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(w.buffer)).decode()}],
        "bufferViews": w.views,
        "accessors": w.accessors,
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(gltf, f, separators=(",", ":"))
        f.write("\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
