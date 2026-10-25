import argparse
import json
import time

import numpy as np

from GenerateDIAFixtures import entity, light, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, read_pfm, run
from ValidateDIADynamics import read_rgba


HISTORY_LIMIT = 400


def count_check(counts, mask, expected, minimum=16):
    samples = counts[mask, :2]
    mismatches = int(np.count_nonzero(np.any(np.abs(samples - expected) > 0.001, axis=-1)))
    return {
        "status": "PASS" if len(samples) >= minimum and mismatches == 0 else "FAIL",
        "pixels": len(samples),
        "mismatches": mismatches,
        "expected": list(expected),
    }


def mirror_edge_check(raw, image):
    mask = raw[..., 3] < 0
    error = float(np.max(np.abs(raw[..., :3][mask] - image[mask]))) if mask.any() else None
    bright = int(np.count_nonzero(mask & (raw[..., :3].max(axis=-1) > 1)))
    dark = int(np.count_nonzero(mask & (raw[..., :3].max(axis=-1) < 0.00001)))
    return {
        "status": "PASS" if error is not None and error < 0.0001 and bright > 0 and dark > 16 else "FAIL",
        "maximum_linear_error": error, "bright_pixels": bright, "dark_pixels": dark,
    }


def wake_check(counts, mask):
    samples = counts[mask, :2]
    minimum = samples.min(axis=0).tolist() if len(samples) else None
    return {
        "status": "PASS" if len(samples) >= 16 and minimum[0] >= HISTORY_LIMIT / 2 else "FAIL",
        "pixels": len(samples), "minimum_diffuse_specular_counts": minimum,
        "minimum_diffuse_count": HISTORY_LIMIT / 2, "minimum_distance_from_disocclusion_pixels": 8,
    }


def pan_count_bounds(roughness, mask, first):
    y, x = np.nonzero(mask)
    if not first:
        y, x = np.meshgrid(np.unique(y), np.arange(63), indexing="ij")
    direction = np.stack((2 * (x + 0.5) / 64 - 1, 1 - 2 * (y + 0.5) / 64, -np.ones_like(x)), axis=-1)
    neighbor = direction.copy()
    neighbor[..., 0] += 0.03125 / 4
    direction /= np.linalg.norm(direction, axis=-1)[..., None]
    neighbor /= np.linalg.norm(neighbor, axis=-1)[..., None]
    drift = np.linalg.norm(direction - neighbor, axis=-1)
    lobe = 1.5 * roughness ** 2
    if np.any(drift > 2 * lobe):
        raise ValueError("Pan oracle requires both planar taps inside the reflection lobe")
    cap = np.minimum(HISTORY_LIMIT, lobe / drift)
    expected = np.minimum(HISTORY_LIMIT, 1 + cap)
    return [float(expected.min()) if first else 1.001, float(expected.max())]


def pan_count_check(counts, mask, roughness, first):
    bounds = pan_count_bounds(roughness, mask, first)
    samples = counts[mask, :2]
    valid = np.abs(samples[:, 0] - HISTORY_LIMIT) <= 0.001
    valid &= (samples[:, 1] >= bounds[0] - (0.001 if first else 0)) & (samples[:, 1] <= bounds[1] + 0.001)
    return {
        "status": "PASS" if len(samples) >= 16 and valid.all() else "FAIL",
        "pixels": len(samples), "mismatches": int(np.count_nonzero(~valid)),
        "expected_diffuse": HISTORY_LIMIT, "expected_specular_bounds": bounds,
        "observed_specular_bounds": [float(samples[:, 1].min()), float(samples[:, 1].max())] if len(samples) else None,
        "roughness": roughness,
    }


def reconstructed_count(normals, weights, view_drift, roughness=0.2):
    normals = np.asarray(normals, dtype=float)
    current = np.array([0.0, 0.0, 1.0])
    lobe = 1.5 * roughness ** 2
    drift = np.sqrt(view_drift ** 2 + 4 * np.sum((normals - current) ** 2, axis=-1))
    valid = drift <= 2 * lobe
    if not valid.any():
        return 0.0
    normals, weights = normals[valid], weights[valid]
    reconstructed = np.sum(normals * weights[:, None], axis=0)
    reconstructed /= np.linalg.norm(reconstructed)
    drift = np.sqrt(view_drift ** 2 + 4 * np.sum((reconstructed - current) ** 2))
    coverage = weights.sum()
    return min(HISTORY_LIMIT, coverage * min(coverage, 1) / np.sum(weights / HISTORY_LIMIT), lobe / max(drift, 1e-6))


def virtual_mirror_check(raw, image, counts, moving):
    identity = raw[..., 3]
    interior = np.zeros(identity.shape, dtype=bool)
    interior[1:-1, 1:-1] = np.all(
        np.lib.stride_tricks.sliding_window_view(identity, (3, 3)) == identity[1:-1, 1:-1, None, None], axis=(-2, -1))
    interior &= identity > 0
    values = counts[..., 1][interior]
    diffuse_counts = counts[..., 0][interior]
    bright = int(np.count_nonzero((identity > 0) & (raw[..., :3].min(axis=-1) > 0.9999)))
    dark = int(np.count_nonzero((identity != 0) & (raw[..., :3].max(axis=-1) < 0.0001)))
    error = float(np.max(np.abs(raw[..., :3] - image)))
    protected = raw[..., :3].max(axis=-1) < 0.0001
    protected_error = float(np.max(np.abs(raw[..., :3][protected] - image[protected]))) if protected.any() else None
    silhouette = int(np.count_nonzero((raw[..., :3].max(axis=-1) > 0.5) != (image.max(axis=-1) > 0.5)))
    retained = bool(len(values) >= 16 and np.max(np.abs(diffuse_counts - HISTORY_LIMIT)) <= 0.001 and
                    (np.min(values) > 1 if moving else np.max(np.abs(values - HISTORY_LIMIT)) <= 0.001))
    return {"status": "PASS" if protected_error is not None and protected_error <= 0.0001 and silhouette == 0 and bright >= 16 and dark >= 16 and retained else "FAIL",
            "scope": "Reflection silhouettes and zero-radiance disocclusion. Lit source interiors contain stochastic indirect light and use the independent motion reference checks for radiometric accuracy.",
            "maximum_linear_error": error, "protected_linear_error": protected_error,
            "bright_source_pixels": bright, "black_bar_pixels": dark,
            "interior_pixels": len(values), "minimum_specular_count": float(values.min()) if len(values) else None,
            "minimum_diffuse_count": float(diffuse_counts.min()) if len(values) else None,
            "silhouette_mismatches": silhouette,
            "expected_count": "greater than 1" if moving else HISTORY_LIMIT}


def reflected_exact_check(exact, reflected):
    white = (reflected[..., 3] == 1) & (np.abs(reflected[..., 2] - 5) < 0.001)
    black = (reflected[..., 3] == 1) & (np.abs(reflected[..., 2] - 4.8) < 0.001)
    error = float(np.max(np.abs(exact[..., :3] - white[..., None])))
    passed = white.sum() >= 16 and black.sum() >= 16 and error <= 0.0001 and np.all(exact[..., 3] == 0)
    return {"status": "PASS" if passed else "FAIL", "maximum_linear_error": error,
            "white_pixels": int(white.sum()), "black_pixels": int(black.sum())}


def self_check():
    def bootstrap(spatial, temporal, center_count, reliable_weight):
        confidence = np.clip((np.asarray(center_count) - 1) / 3, 0, 1) * np.clip(reliable_weight, 0, 1)
        return spatial * (1 - confidence) + temporal * confidence

    assert bootstrap(9, 3, 1, 25) == 9
    assert np.isclose(bootstrap(9, 3, 2, 25), 7)
    assert np.isclose(bootstrap(9, 3, 3, 25), 5)
    assert bootstrap(9, 3, 4, 25) == 3
    assert bootstrap(9, 3, HISTORY_LIMIT, 0) == 9
    assert np.isclose(bootstrap(9, 3, 2, 0.5), 8)
    assert np.array_equal(bootstrap(9, 3, [1, HISTORY_LIMIT], 25), [9, 3])
    counts = np.ones((8, 8, 4), dtype=np.float32)
    mask = np.ones((8, 8), dtype=bool)
    assert count_check(counts, mask, (1, 1))["status"] == "PASS"
    counts[..., :2] = (HISTORY_LIMIT, 1)
    assert count_check(counts, mask, (HISTORY_LIMIT, 1))["status"] == "PASS"
    assert count_check(counts, mask, (1, 1))["status"] == "FAIL"
    assert count_check(counts, mask & False, (HISTORY_LIMIT, 1))["status"] == "FAIL"
    assert wake_check(counts, mask)["status"] == "PASS"
    counts[..., 0] = HISTORY_LIMIT / 10
    assert wake_check(counts, mask)["status"] == "FAIL"
    raw = np.zeros((8, 8, 4), dtype=np.float32)
    raw[..., 3] = -1
    raw[:4, :, :3] = 8
    image = raw[..., :3].copy()
    assert mirror_edge_check(raw, image)["status"] == "PASS"
    image[4] = 1
    assert mirror_edge_check(raw, image)["status"] == "FAIL"
    mask = np.zeros((64, 64), dtype=bool)
    mask[2:6, 20:40] = True
    counts = np.full((64, 64, 4), HISTORY_LIMIT, dtype=np.float64)
    for roughness in (0.7, 0.1):
        lower, upper = pan_count_bounds(roughness, mask, True)
        assert 1 < lower < upper < HISTORY_LIMIT
        counts[..., 1] = (lower + upper) / 2
        assert pan_count_check(counts, mask, roughness, True)["status"] == "PASS"
        assert pan_count_check(counts, mask, roughness, False)["status"] == "PASS"
        for rejected in (1, HISTORY_LIMIT):
            counts[..., 1] = rejected
            assert pan_count_check(counts, mask, roughness, True)["status"] == "FAIL"
            assert pan_count_check(counts, mask, roughness, False)["status"] == "FAIL"
    assert pan_count_bounds(0.1, mask, True)[1] < pan_count_bounds(0.7, mask, True)[0]
    angles = np.deg2rad([-1.2, 1.2])
    normals = np.stack((np.sin(angles), np.zeros(2), np.cos(angles)), axis=-1)
    weights = np.array([0.5, 0.5])
    view_drift = np.deg2rad(0.1136)
    assert reconstructed_count(np.array([[0, 0, 1]]), np.array([1]), 0) == HISTORY_LIMIT
    assert 30 < reconstructed_count(normals, weights, view_drift) < 31
    assert reconstructed_count(np.repeat(normals[1:2], 2, axis=0), weights, view_drift) < 2
    angle = np.deg2rad(6)
    assert reconstructed_count(np.array([[np.sin(angle), 0, np.cos(angle)]]), np.array([1]), 0) == 0
    raw = np.zeros((12, 12, 4))
    raw[..., 3] = 1
    raw[:6, :, :3] = 1
    raw[6:, :, 3] = 2
    image = raw[..., :3].copy()
    counts = np.full((12, 12, 4), HISTORY_LIMIT)
    assert virtual_mirror_check(raw, image, counts, False)["status"] == "PASS"
    counts[..., 1] = 2
    assert virtual_mirror_check(raw, image, counts, True)["status"] == "PASS"
    assert virtual_mirror_check(raw, image, counts, False)["status"] == "FAIL"
    counts[..., 1] = 1
    assert virtual_mirror_check(raw, image, counts, True)["status"] == "FAIL"
    counts[..., 1] = 2
    image[6] = 1
    assert virtual_mirror_check(raw, image, counts, True)["status"] == "FAIL"
    reflected = np.zeros((8, 8, 4))
    reflected[..., 3] = 1
    reflected[..., 2] = 4.8
    reflected[:4, :, 2] = 5
    exact = np.zeros_like(reflected)
    exact[:4, :, :3] = 1
    assert reflected_exact_check(exact, reflected)["status"] == "PASS"
    exact[4:, :, :3] = 0.1
    assert reflected_exact_check(exact, reflected)["status"] == "FAIL"


def motion_checks(frozen, output):
    checks = {}
    events = [(HISTORY_LIMIT + 99, "capture before")]
    for index in range(32):
        step = index + 1 if index < 16 else 31 - index
        events.append((HISTORY_LIMIT + 100 + index, f'set "Temporal camera" Transform Position {step / 32} 0 4'))
        if index in (0, 15, 31):
            events.append((HISTORY_LIMIT + 100 + index, f"capture pan{index + 1}"))
    for projection, scene, roughness in (("perspective", "Temporal", 0.7), ("orthographic", "TemporalOrthographic", 0.7),
                                         ("glossy", "TemporalGlossy", 0.1)):
        folder = output / ("pan" if projection == "perspective" else f"{projection}-pan")
        folder.mkdir()
        sequence = folder / "events.txt"
        sequence.write_text("\n".join(f"at {frame} {action}" for frame, action in events) + "\n")
        run(frozen / "Binaries/Release/Runtime.exe", [
            f"Scenes/DIA/{scene}.scene", "--width", 64, "--height", 64,
            "--frames", HISTORY_LIMIT + 132, "--warmup", 0, "--seed", 911,
            "--sequence", sequence, "--capture-dir", folder,
        ], folder / "runtime.log")
        for label in ("before", "pan1", "pan16", "pan32"):
            counts = read_rgba(folder / f"{label}.history-counts.rgba")
            mask = np.zeros(counts.shape[:2], dtype=bool)
            mask[2:6, 20:40] = True
            name = label if projection == "perspective" else f"{projection}_{label}"
            checks[name] = count_check(counts, mask, (HISTORY_LIMIT, HISTORY_LIMIT)) if label == "before" or projection == "orthographic" else pan_count_check(counts, mask, roughness, label == "pan1")
        mask = np.zeros(counts.shape[:2], dtype=bool)
        mask[20:44, 52:56] = True
        checks[f"{projection}_count_wake"] = wake_check(counts, mask)

    folder = output / "textured-mirror"
    folder.mkdir()
    sequence = folder / "events.txt"
    sequence.write_text(
        f'at 0 capture initial\nat 1 capture second\nat {HISTORY_LIMIT - 1} capture steady\n'
        f'at {HISTORY_LIMIT} set "Temporal camera" Transform Position 0.03125 0 4\nat {HISTORY_LIMIT} capture moved\n'
    )
    run(frozen / "Binaries/Release/Runtime.exe", [
        "Scenes/DIA/TemporalMirror.scene", "--width", 64, "--height", 64,
        "--frames", HISTORY_LIMIT + 1, "--warmup", 0, "--seed", 911,
        "--sequence", sequence, "--capture-dir", folder,
    ], folder / "runtime.log")
    for label, expected in (("initial", (1, 1)), ("second", (2, 2)), ("steady", (HISTORY_LIMIT, HISTORY_LIMIT))):
        raw = read_rgba(folder / f"{label}.raw1.rgba")
        counts = read_rgba(folder / f"{label}.history-counts.rgba")
        checks[f"textured_mirror_{label}"] = count_check(counts, raw[..., 3] < 0, expected)
    for label in ("steady", "moved"):
        raw = read_rgba(folder / f"{label}.raw1.rgba").copy()
        raw[..., :3] += read_rgba(folder / f"{label}.raw4.rgba")[..., :3]
        checks[f"mirror_edges_{label}"] = mirror_edge_check(raw, read_pfm(folder / f"{label}.pfm"))
    raw = read_rgba(folder / "moved.raw1.rgba")
    counts = read_rgba(folder / "moved.history-counts.rgba")
    checks["moving_mirror_rejects_stale_reflection"] = count_check(counts, raw[..., 3] < 0, (HISTORY_LIMIT, 1))

    folder = output / "virtual-mirror"
    folder.mkdir()
    sequence = folder / "events.txt"
    sequence.write_text(
        f'at {HISTORY_LIMIT - 1} capture steady\n'
        f'at {HISTORY_LIMIT} set "Temporal camera" Transform Position 0.03125 0 4\nat {HISTORY_LIMIT} capture moved\n'
    )
    run(frozen / "Binaries/Release/Runtime.exe", [
        "Scenes/DIA/TemporalVirtualMirror.scene", "--width", 64, "--height", 64,
        "--frames", HISTORY_LIMIT + 1, "--warmup", 0, "--seed", 911,
        "--sequence", sequence, "--capture-dir", folder,
    ], folder / "runtime.log")
    for label in ("steady", "moved"):
        raw = read_rgba(folder / f"{label}.raw1.rgba").copy()
        raw[..., :3] += read_rgba(folder / f"{label}.raw4.rgba")[..., :3]
        checks[f"virtual_mirror_{label}"] = virtual_mirror_check(
            raw, read_pfm(folder / f"{label}.pfm"),
            read_rgba(folder / f"{label}.history-counts.rgba"), label == "moved")
    folder = output / "exact-mirror"
    folder.mkdir()
    sequence = folder / "events.txt"
    sequence.write_text('at 0 capture initial\nat 1 set "Temporal camera" Transform Position 0.03125 0 4\nat 1 capture moved\n')
    run(frozen / "Binaries/Release/Runtime.exe", [
        "Scenes/DIA/TemporalExactMirror.scene", "--width", 64, "--height", 64,
        "--frames", 2, "--warmup", 0, "--seed", 911, "--sequence", sequence, "--capture-dir", folder,
    ], folder / "runtime.log")
    for label in ("initial", "moved"):
        checks[f"reflected_exact_{label}"] = reflected_exact_check(
            read_rgba(folder / f"{label}.raw4.rgba"), read_rgba(folder / f"{label}.raw5.rgba"))
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("Temporal metric self-check PASS")
        return

    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-temporal")
    output.mkdir(parents=True)
    frozen, _ = freeze(output)
    camera = entity("Temporal camera", {
        "Name": {"Name": '"Temporal camera"'},
        "Transform": {"Position": "0 0 4", "Rotation": "0 0 0"},
        "Camera": {"Perspective": "true", "FieldOfView": "90", "NearClip": "0.1", "FarClip": "128"},
        "DIA": {
            "Enabled": "true", "SkyColor": "1 1 1 1", "SkyIntensity": "1",
            "TraceDistance": "128", "DiffuseSamples": "1", "ReflectionSamples": "1",
            "CacheUpdates": "4096", "InternalHeight": "64",
        },
    })
    material = frozen / "Content/Materials/DIA/Temporal.material"
    material.write_text('Shader "Lit"\nBaseColor 0.75 0.75 0.75 1\nMetallic 0\nRoughness 0.7\n')
    scene = frozen / "Content/Scenes/DIA/Temporal.scene"
    scene.write_text("version 2\n\n" + "\n\n".join([
        camera,
        mesh("Temporal receiver", "0 0 0", "16 16 1", "Temporal", shape="Quad"),
        mesh("Temporal occluder", "0 0 2", "1.5 3 1", "Temporal", shape="Quad"),
    ]) + "\n")
    (frozen / "Content/Scenes/DIA/TemporalOrthographic.scene").write_text(
        scene.read_text().replace("Perspective true", "Perspective false\n  Zoom 8")
    )
    (frozen / "Content/Materials/DIA/TemporalGlossy.material").write_text(
        material.read_text().replace("Roughness 0.7", "Roughness 0.1")
    )
    (frozen / "Content/Scenes/DIA/TemporalGlossy.scene").write_text(
        scene.read_text().replace("Materials/DIA/Temporal.material", "Materials/DIA/TemporalGlossy.material")
    )
    (frozen / "Content/Materials/DIA/TemporalMirror.material").write_text(
        'Shader "Lit"\nBaseColor 1 1 1 1\nMetallic 1\nRoughness 0.01\n'
    )
    (frozen / "Content/Materials/DIA/TemporalPattern.material").write_text(
        'Shader "Lit"\nBaseColor 0 0 0 1\nSpecular 0\nRoughness 0.7\n'
        'BaseColorMap "Textures/DIAPatch.png"\nEmissiveColor 1 1 1 1\n'
        'EmissiveIntensity 8\nEmissiveMap "Textures/DIAPatch.png"\n'
    )
    (frozen / "Content/Scenes/DIA/TemporalMirror.scene").write_text("version 2\n\n" + "\n\n".join([
        camera.replace("SkyIntensity 1", "SkyIntensity 0"),
        mesh("Temporal mirror", "0 0 0", "16 16 1", "TemporalMirror", shape="Quad"),
        mesh("Temporal pattern", "0 0 5", "12 12 1", "TemporalPattern", rotation="0 180 0", shape="Quad"),
    ]) + "\n")
    for name, color, roughness in (("TemporalVirtualWhite", "1 1 1", 0.7), ("TemporalVirtualBlack", "0 0 0", 0)):
        (frozen / f"Content/Materials/DIA/{name}.material").write_text(
            f'Shader "Lit"\nBaseColor {color} 1\nSpecular 0\nRoughness {roughness}\nMetallic 0\nTwoSided false\n')
    light_slope = 40
    light_yaw = np.rad2deg(np.arctan2(light_slope, -1))
    light_intensity = np.pi * np.sqrt(1 + light_slope ** 2)
    assert 0.2 * light_slope > 6 + 0.15
    assert 5 * light_slope > 6 + 8
    (frozen / "Content/Scenes/DIA/TemporalVirtualMirror.scene").write_text("version 2\n\n" + "\n\n".join([
        camera.replace("SkyIntensity 1", "SkyIntensity 0"),
        light("DirectionalLight", intensity=light_intensity, rotation=f"0 {light_yaw:.9g} 0"),
        mesh("Virtual mirror", "0 0 0", "16 16 1", "TemporalMirror", shape="Quad"),
        mesh("Virtual white source", "0 0 5", "12 12 1", "TemporalVirtualWhite", rotation="0 180 0", shape="Quad"),
        mesh("Virtual black bar", "0 0 4.8", "0.3 12 1", "TemporalVirtualBlack", rotation="0 180 0", shape="Quad"),
    ]) + "\n")
    (frozen / "Content/Materials/DIA/TemporalExactWhite.material").write_text(
        'Shader "Lit"\nBaseColor 1 1 1 1\nSpecular 0\nRoughness 0\nMetallic 0\nTwoSided false\n')
    (frozen / "Content/Scenes/DIA/TemporalExactMirror.scene").write_text(
        (frozen / "Content/Scenes/DIA/TemporalVirtualMirror.scene").read_text().replace(
            "Materials/DIA/TemporalVirtualWhite.material", "Materials/DIA/TemporalExactWhite.material"))
    hashes = fingerprint(frozen)
    events = [
        (HISTORY_LIMIT + 99, "capture before"),
        (HISTORY_LIMIT + 100, 'set "Temporal camera" Transform Position 1 0 4'),
        (HISTORY_LIMIT + 100, "capture revealed0"),
        (HISTORY_LIMIT + 101, "capture revealed1"),
        (HISTORY_LIMIT + 129, "capture revealed29"),
        (2 * HISTORY_LIMIT + 99, "capture steady"),
        (2 * HISTORY_LIMIT + 100, 'set "Temporal camera" DIA SkyIntensity 0.5'),
        (2 * HISTORY_LIMIT + 100, "capture revised0"),
        (2 * HISTORY_LIMIT + 101, "capture revised1"),
        (2 * HISTORY_LIMIT + 110, "cut"),
        (2 * HISTORY_LIMIT + 110, "capture cut0"),
        (2 * HISTORY_LIMIT + 111, "capture cut1"),
    ]
    sequence = output / "events.txt"
    sequence.write_text("\n".join(f"at {frame} {action}" for frame, action in events) + "\n")
    report = {"source_hashes": hashes, "environment": environment(), "history_limit": HISTORY_LIMIT, "checks": {}}
    print("Evidence:", output, flush=True)
    try:
        report["run"] = run(frozen / "Binaries/Release/Runtime.exe", [
            "Scenes/DIA/TemporalGlossy.scene", "--width", 64, "--height", 64,
            "--frames", 2 * HISTORY_LIMIT + 112, "--warmup", 0, "--seed", 911,
            "--sequence", sequence, "--capture-dir", output,
        ], output / "runtime.log")
        labels = [action.split()[1] for _, action in events if action.startswith("capture ")]
        counts = {label: read_rgba(output / f"{label}.history-counts.rgba") for label in labels}
        positions = {label: read_rgba(output / f"{label}.raw2.rgba") for label in ("before", "revealed0")}
        identities = {label: read_rgba(output / f"{label}.raw0.rgba")[..., 3] for label in positions}
        receiver = (identities["revealed0"] > 0) & (np.abs(positions["revealed0"][..., 2]) < 0.001)
        previous_identity = np.zeros_like(identities["before"])
        previous_identity[:, :-8] = identities["before"][:, 8:]
        in_previous_view = np.indices(receiver.shape)[1] < 56
        revealed = receiver & in_previous_view & (previous_identity != identities["revealed0"])
        retained = receiver & in_previous_view & (previous_identity == identities["revealed0"])
        checks = report["checks"]
        for label, expected in (("revealed0", (1, 1)), ("revealed1", (2, 2)),
                                ("revealed29", (30, 30)), ("steady", (HISTORY_LIMIT, HISTORY_LIMIT))):
            checks[label] = count_check(counts[label], revealed, expected)
        checks["independent_specular_reset"] = count_check(counts["revealed0"], retained, (HISTORY_LIMIT, 1))
        checks["independent_specular_restart"] = count_check(counts["revealed1"], retained, (HISTORY_LIMIT, 2))
        checks["retained_cap"] = count_check(counts["steady"], retained, (HISTORY_LIMIT, HISTORY_LIMIT))
        for label, expected in (("revised0", (1, 1)), ("revised1", (2, 2)),
                                ("cut0", (1, 1)), ("cut1", (2, 2))):
            checks[label] = count_check(counts[label], receiver, expected)
        metadata = {label: json.loads((output / f"{label}.json").read_text()) for label in labels}
        revisions = {metadata[label]["scene_revision"] for label in ("before", "revealed0", "revealed1", "steady")}
        revision_reset = metadata["revised0"]["scene_revision"] > metadata["steady"]["scene_revision"]
        checks["camera_preserves_scene"] = {"status": "PASS" if len(revisions) == 1 else "FAIL"}
        checks["scene_revision_changes"] = {"status": "PASS" if revision_reset else "FAIL"}
        checks["residency_budget"] = {
            "status": "PASS" if all(data["resident_bytes_bound"] <= 256 * 1024 * 1024 for data in metadata.values()) else "FAIL",
        }
        checks.update(motion_checks(frozen, output))
        assert fingerprint(frozen) == hashes
        report["status"] = "PASS" if all(check["status"] == "PASS" for check in checks.values()) else "FAIL"
    except Exception as error:
        report["status"] = "FAIL"
        report["failure"] = str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(report.get("status", "FAIL"), "Evidence:", output, flush=True)
    if report["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
