# Makes the space sky for Dead Air Freight:
#
#   Content/Environments/Space.png       the skybox panorama: nebulae and a faint galaxy band
#   Content/Meshes/Stars/Stars*.obj      the starfield, as meshes of tiny quads on a sphere
#
# The panorama is an ordinary 2:1 PNG, so it can be painted over in any image editor afterwards.
# Nebulae are cloud noise recolored: fractal "cloud" noise is shaped by a warped soft mask, given
# filaments and dark dust lanes, and tinted along a gas palette. Stars are geometry rather than
# pixels because the engine shrinks skyboxes to 512 pixels wide, which would blur them away.
#
# Run from anywhere:  python Projects/DeadAirFreight/Tools/MakeSky.py
# The panorama takes a few minutes; add --stars to remake only the stars.
# Needs numpy and Pillow. The same seed always makes the same sky.

import math
import os
import sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CONTENT = os.path.join(HERE, '..', 'Content')
WIDTH, HEIGHT = 2048, 1024
SEED = 7

# Directions in the engine's panorama: u = atan2(z, x) / 2pi + 0.5, v = acos(y) / pi.
#   -Z, out of the cab's windshield:        u 0.25
#   +X, the title screen's view:             u 0.50
#   -X, behind the truck in the lobby view:  u 0.00 / 1.00
def direction(u, v):
    theta = (u - 0.5) * 2 * math.pi
    phi = v * math.pi
    return np.array([math.cos(theta) * math.sin(phi), math.cos(phi), math.sin(theta) * math.sin(phi)])

# Each nebula: where it sits, how big (radians), its gas colors from thin to dense, and how bright.
NEBULAE = [
    # Title screen: a big magenta and blue cloud, a little above the horizon.
    dict(at=(0.50, 0.40), size=0.75, thin=(0.10, 0.20, 0.75), dense=(0.95, 0.25, 0.75), core=(1.0, 0.8, 0.95), bright=0.55, seed=1),
    # Behind the truck in the lobby: teal and violet.
    dict(at=(0.98, 0.53), size=0.6, thin=(0.25, 0.10, 0.6), dense=(0.10, 0.75, 0.80), core=(0.8, 1.0, 1.0), bright=0.5, seed=2),
    # Ahead of the truck, toward the colony: a red and orange emission nebula.
    dict(at=(0.25, 0.47), size=0.55, thin=(0.45, 0.05, 0.15), dense=(1.0, 0.45, 0.15), core=(1.0, 0.9, 0.6), bright=0.5, seed=3),
    # Smaller wisps to fill the gaps.
    dict(at=(0.70, 0.62), size=0.35, thin=(0.05, 0.15, 0.4), dense=(0.3, 0.5, 1.0), core=(0.9, 0.95, 1.0), bright=0.3, seed=4),
    dict(at=(0.12, 0.30), size=0.3, thin=(0.3, 0.05, 0.35), dense=(0.8, 0.3, 0.9), core=(1.0, 0.85, 1.0), bright=0.25, seed=5),
    dict(at=(0.40, 0.75), size=0.3, thin=(0.05, 0.25, 0.25), dense=(0.2, 0.8, 0.55), core=(0.85, 1.0, 0.9), bright=0.22, seed=6),
]
# The galaxy band, a tilted great circle, and the stars crowd toward it.
BAND_NORMAL = np.array([0.35, 0.85, 0.4]) / np.linalg.norm([0.35, 0.85, 0.4])


def hash3(ix, iy, iz, seed):
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 1274126177) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)).astype(np.float64) / 0xFFFFFFFF


def value_noise(p, seed):
    """Smooth 3D value noise in 0..1 at points p (N x 3)."""
    i = np.floor(p).astype(np.int64)
    f = p - i
    f = f * f * (3 - 2 * f)
    total = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                total = total + w * hash3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz, seed)
    return total


def clouds(p, seed, octaves=6):
    """Fractal cloud noise in about 0..1."""
    total, amplitude, frequency, norm = 0, 0.5, 1.0, 0
    for o in range(octaves):
        total = total + amplitude * value_noise(p * frequency, seed + o * 31)
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.03
    return total / norm


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def make_panorama():
    u = (np.arange(WIDTH) + 0.5) / WIDTH
    v = (np.arange(HEIGHT) + 0.5) / HEIGHT
    uu, vv = np.meshgrid(u, v)
    theta = (uu - 0.5) * 2 * np.pi
    phi = vv * np.pi
    d = np.stack([np.cos(theta) * np.sin(phi), np.cos(phi), np.sin(theta) * np.sin(phi)], axis=-1).reshape(-1, 3)
    color = np.zeros_like(d)

    # A faint galaxy band of dust and unresolved stars.
    across = d @ BAND_NORMAL
    band = np.exp(-(across / 0.22) ** 2) * clouds(d * 3 + 11, SEED + 100)
    band = smoothstep(0.35, 0.8, band) * 0.09
    dust = smoothstep(0.45, 0.7, clouds(d * 6 + 23, SEED + 200))
    color += band[:, None] * (1 - 0.8 * dust[:, None]) * np.array([0.75, 0.8, 1.0])

    for n in NEBULAE:
        center = direction(*n['at'])
        s = SEED + n['seed'] * 1000
        # Warp the directions with noise first, so the outline billows like a cloud instead of a disc.
        warp = np.stack([clouds(d * 2 + k * 7.3, s + k, 4) for k in range(3)], axis=-1) - 0.5
        warped = d + warp * 0.5 * n['size']
        warped /= np.linalg.norm(warped, axis=1, keepdims=True)
        angle = np.arccos(np.clip(warped @ center, -1, 1))
        mask = np.exp(-(angle / n['size']) ** 2 * 2.2)
        # The cloud itself, sharpened into wisps, with brighter filaments through it.
        body = clouds(d * 4 + s * 0.01, s + 10)
        wisps = smoothstep(0.38, 0.75, body)
        ridged = 1 - np.abs(clouds(d * 9 + 5, s + 20) * 2 - 1)
        filaments = smoothstep(0.75, 0.95, ridged) * 0.6
        density = mask * (wisps + filaments * wisps)
        # Dark lanes of dust cut through it.
        lanes = smoothstep(0.5, 0.72, clouds(d * 7 + 3, s + 30))
        density *= 1 - 0.75 * lanes
        # Thin gas is one color, denser gas another, and the very densest glows near white.
        tint = clouds(d * 3 + 9, s + 40)[:, None]
        gas = np.array(n['thin']) * (1 - tint) + np.array(n['dense']) * tint
        core = smoothstep(0.55, 1.0, density)[:, None]
        gas = gas * (1 - core) + np.array(n['core']) * core
        color += gas * density[:, None] * n['bright']

    color = np.clip(color, 0, 1).reshape(HEIGHT, WIDTH, 3)
    # Stored as sRGB like any picture; the engine turns it back into light.
    srgb = np.where(color <= 0.0031308, color * 12.92, 1.055 * np.power(color, 1 / 2.4) - 0.055)
    os.makedirs(os.path.join(CONTENT, 'Environments'), exist_ok=True)
    Image.fromarray((srgb * 255 + 0.5).astype(np.uint8)).save(os.path.join(CONTENT, 'Environments', 'Space.png'))


def make_stars(name, count, size_min, size_max, band_bias, seed):
    """A sphere of radius 1 sprinkled with tiny quads facing its middle, so one mesh is many stars."""
    rng = np.random.default_rng(seed)
    stars = []
    while len(stars) < count:
        p = rng.normal(size=3)
        p /= np.linalg.norm(p)
        # More stars toward the galaxy band.
        if rng.random() > (1 - band_bias) + band_bias * math.exp(-(p @ BAND_NORMAL / 0.3) ** 2):
            continue
        stars.append(p)
    v, f = [], []
    for p in stars:
        size = rng.uniform(size_min, size_max) * (rng.random() ** 2 * 0.8 + 0.6)
        helper = np.array([0, 1, 0]) if abs(p[1]) < 0.95 else np.array([1, 0, 0])
        a = np.cross(p, helper)
        a /= np.linalg.norm(a)
        b = np.cross(p, a)
        twist = rng.uniform(0, math.pi)
        a, b = a * math.cos(twist) + b * math.sin(twist), b * math.cos(twist) - a * math.sin(twist)
        base = len(v)
        for sa, sb in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            v.append(p + (a * sa + b * sb) * size)
        # Both windings, so the stars show whichever way the faces are culled.
        f.append((base, base + 1, base + 2, base + 3))
        f.append((base + 3, base + 2, base + 1, base))
    path = os.path.join(CONTENT, 'Meshes', 'Stars', name + '.obj')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as out:
        out.write('# Starfield made by Tools/MakeSky.py\no %s\n' % name)
        for q in v:
            out.write('v %.6f %.6f %.6f\n' % tuple(q))
            n = -q / np.linalg.norm(q)
            out.write('vn %.4f %.4f %.4f\n' % tuple(n))
        out.write('vt 0.5 0.5\n')
        for face in f:
            out.write('f ' + ' '.join('%d/1/%d' % (i + 1, i + 1) for i in face) + '\n')


if __name__ == '__main__':
    if '--stars' not in sys.argv:
        make_panorama()
    # Sizes are in radians across: about 1 to 2 pixels at the game's field of view.
    make_stars('StarsDim', 2600, 0.0008, 0.0013, 0.5, SEED + 1)
    make_stars('StarsBright', 320, 0.0014, 0.0022, 0.3, SEED + 2)
    make_stars('StarsTinted', 380, 0.0010, 0.0016, 0.4, SEED + 3)
    print('Made the sky panorama and starfield.')
