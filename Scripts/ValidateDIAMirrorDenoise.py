import argparse
import json
import shutil
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image

from GenerateDIAFixtures import entity, light, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, input_files, read_pfm, run
from ValidateDIADynamics import read_rgba
from ValidateDIAMotion import gradient_check, noise_metrics, reference_check


SIZE = 64
SEEDS = [911, 1237, 3217]
GATES = {"temporal_ratio": 0.9, "spatial_ratio": 1.0, "mean_change": 0.03, "bypass_error": 1e-5}


def unchanged(before, after, mask, minimum=16):
    error = float(np.max(np.abs(before[mask] - after[mask]))) if mask.any() else None
    return {"status": "PASS" if mask.sum() >= minimum and error <= GATES["bypass_error"] else "FAIL",
            "pixels": int(mask.sum()), "maximum_error": error}


def plane(points):
    dx, dy = points[..., 1, :] - points[..., 0, :], points[..., 3, :] - points[..., 2, :]
    area = np.cross(dx, dy)
    squared = np.sum(area * area, axis=-1)
    valid = squared > np.maximum(1e-20, 1e-6 * np.sum(dx * dx, axis=-1) * np.sum(dy * dy, axis=-1))
    normal = area / np.sqrt(np.maximum(squared[..., None], 1e-30))
    tolerance = np.maximum(1e-5, np.minimum(0.002, 0.02 * np.maximum(np.linalg.norm(dx, axis=-1), np.linalg.norm(dy, axis=-1))))
    residual = np.max(np.abs(np.sum(points * normal[..., None, :], axis=-1)), axis=-1)
    return valid & np.isfinite(points).all(axis=(-1, -2)) & (residual <= tolerance)


def mapped_receiver_check(before, after, raw):
    mask = (raw[0][..., 3] < 0) & (np.abs(raw[3][..., 3] - 0.01) < 1e-6) & (raw[5][..., 3] == 1)
    reflected = float(np.max(raw[1][..., :3][mask])) if mask.any() else 0.0
    illuminated = int(np.count_nonzero(mask & (raw[1][..., :3].max(axis=-1) > 1e-5)))
    identical = np.array_equal(before[mask], after[mask])
    return {"status": "PASS" if mask.all() and illuminated >= 16 and identical else "FAIL",
            "mapped_mirror_pixels": int(mask.sum()), "maximum_reflected_radiance": reflected,
            "illuminated_reflection_pixels": illuminated, "bitwise_rgb_bypass": identical}


def crease_mask(raw, reflected):
    ids, source = raw[0][..., 3], raw[1][..., 3]
    compatible = (ids > 0) & (source > 0) & (reflected[..., 3] == 1) & (raw[3][..., 3] <= 0.02)
    points = []
    for dy, dx in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        shifted = lambda value: np.roll(value, (-dy, -dx), axis=(0, 1))
        compatible &= (ids == shifted(ids)) & (source == shifted(source)) & (shifted(reflected)[..., 3] == 1)
        points.append(shifted(reflected)[..., :3] - reflected[..., :3])
    compatible[[0, -1], :] = False
    compatible[:, [0, -1]] = False
    return compatible & ~plane(np.stack(points, axis=-2))


def self_check():
    points = np.array([[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0]], dtype=float)
    assert plane(points)
    points[[0, 1], 2] = 0.1
    assert not plane(points)
    assert not plane(points * 0)
    before = np.zeros((8, 8, 3))
    mask = np.ones((8, 8), dtype=bool)
    assert unchanged(before, before, mask)["status"] == "PASS"
    assert unchanged(before, before + 0.01, mask)["status"] == "FAIL"
    assert unchanged(before, before, mask & False)["status"] == "FAIL"
    raw = [np.zeros((8, 8, 4)) for _ in range(6)]
    raw[0][..., 3], raw[3][..., 3], raw[5][..., 3], raw[1][..., :3] = -1, 0.01, 1, 0.1
    assert mapped_receiver_check(before, before, raw)["status"] == "PASS"
    assert mapped_receiver_check(before, before + 0.001, raw)["status"] == "FAIL"
    raw[0][..., 3] = 1
    assert mapped_receiver_check(before, before, raw)["status"] == "FAIL"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for folder in ("Materials/DIA", "Scenes/DIA", "Textures"):
            (root / "Content" / folder).mkdir(parents=True)
        fixtures(root)
        receiver = (root / "Content/Materials/DIA/MirrorDenoiseReceiverMap.material").read_text()
        source = (root / "Content/Materials/DIA/MirrorDenoiseMappedReceiver.material").read_text()
        scene = (root / "Content/Scenes/DIA/MirrorDenoiseMappedReceiver.scene").read_text()
        assert "Roughness 0.01" in receiver and "BaseColorMap" in receiver and "Metallic 1" in receiver
        assert "Roughness 0.7" in source and "BaseColorMap" not in source
        assert scene.count('Material "Materials/DIA/MirrorDenoiseReceiverMap.material"') == 1
        assert scene.count('Material "Materials/DIA/MirrorDenoiseMappedReceiver.material"') == 1


def fixtures(root):
    materials = root / "Content/Materials/DIA"
    camera = entity("Mirror denoise camera", {
        "Name": {"Name": '"Mirror denoise camera"'},
        "Transform": {"Position": "0 0 4", "Rotation": "0 0 0"},
        "Camera": {"Perspective": "true", "FieldOfView": "90", "NearClip": "0.1", "FarClip": "128"},
        "DIA": {"Enabled": "true", "SkyColor": "1 1 1 1", "SkyIntensity": "1", "TraceDistance": "128",
                  "DiffuseSamples": "1", "ReflectionSamples": "1", "CacheUpdates": "4096", "InternalHeight": "64"},
    })
    mirror = 'Shader "Lit"\nBaseColor 1 1 1 1\nMetallic 1\nRoughness 0.01\n'
    source = 'Shader "Lit"\nBaseColor 0.75 0.75 0.75 1\nMetallic 0\nRoughness 0.7\nSpecular 1\n'
    checker = np.indices((8, 8)).sum(axis=0) % 2 * 255
    texture = root / "Content/Textures/MirrorDenoise.png"
    Image.fromarray(np.repeat(checker.astype(np.uint8)[..., None], 3, axis=-1)).save(texture)
    mapped = 'BaseColorMap "Textures/MirrorDenoise.png"\n'
    (materials / "MirrorDenoiseMirror.material").write_text(mirror)
    (materials / "MirrorDenoiseReceiverMap.material").write_text(mirror + mapped)
    (materials / "MirrorDenoiseBlack.material").write_text(source.replace("0.75 0.75 0.75", "0 0 0").replace("Specular 1", "Specular 0").replace("Roughness 0.7", "Roughness 0"))
    variants = {"Noisy": source, "Folded": source, "Mapped": source + mapped,
                "Sharp": source.replace("Roughness 0.7", "Roughness 0.01"),
                "Medium": source.replace("Roughness 0.7", "Roughness 0.3"),
                "Emission": source.replace("Specular 1", "Specular 0").replace("0.75 0.75 0.75", "0 0 0") + "EmissiveColor 1 1 1 1\nEmissiveIntensity 2\n",
                "ExactShadow": source.replace("Metallic 0", "Metallic 1"),
                "MappedReceiver": source, "CurvedReceiver": source}
    for name, text in variants.items():
        (materials / f"MirrorDenoise{name}.material").write_text(text)
        receiver = "MirrorDenoiseReceiverMap" if name == "MappedReceiver" else "MirrorDenoiseMirror"
        blocks = [camera.replace("SkyIntensity 1", "SkyIntensity 0") if name in ("Emission", "ExactShadow") else camera,
                  mesh("Mirror denoise receiver", "0 0 0", "3 3 3" if name == "CurvedReceiver" else "16 16 1",
                       receiver, shape="Sphere" if name == "CurvedReceiver" else "Quad"),
                  mesh("Mirror denoise source", "0 0 9" if name == "Folded" else "0 0 5",
                       "8 20 4" if name == "Folded" else "20 20 1", f"MirrorDenoise{name}",
                       rotation="0 35 0" if name == "Folded" else "0 180 0", shape="Cube" if name == "Folded" else "Quad")]
        if name in ("Noisy", "ExactShadow"):
            blocks += [mesh("Mirror denoise bar", "1 0 3", "0.5 4 0.2", "MirrorDenoiseBlack"),
                       light("PointLight", "-3 2 1", 20)]
        if name == "Emission":
            blocks.append(mesh("Mirror emission bar", "0 0 4.8", "0.5 12 0.1", "MirrorDenoiseBlack"))
        (root / f"Content/Scenes/DIA/MirrorDenoise{name}.scene").write_text("version 2\n\n" + "\n\n".join(blocks) + "\n")


def capture(folder, label):
    image = read_pfm(folder / f"{label}.pfm")
    raw = [read_rgba(folder / f"{label}.raw{index}.rgba") for index in range(6)]
    raw.append(read_rgba(folder / f"{label}.history-counts.rgba"))
    if image.shape != (SIZE, SIZE, 3) or not np.isfinite(image).all() or any(not np.isfinite(item).all() for item in raw) or np.any(raw[4][..., 3] != 0):
        raise ValueError(f"Invalid capture: {folder / label}")
    return image, raw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--reference-spp", type=int, default=8192)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("Mirror denoise metric self-check PASS")
        return
    if args.baseline is None or not 1 <= args.reference_spp <= 16384:
        parser.error("--baseline and reference SPP between 1 and 16384 are required")
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-mirror-denoise")
    output.mkdir(parents=True)
    candidate, original_candidate = freeze(output)
    original_baseline = fingerprint(args.baseline)
    baseline = output / "baseline-frozen"
    baseline_files = input_files(args.baseline)
    for name in original_baseline:
        target = baseline / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(baseline_files[name], target)
    if fingerprint(baseline) != original_baseline:
        raise ValueError("Baseline changed during copying")
    sources = {"baseline": baseline, "candidate": candidate}
    for root in sources.values():
        fixtures(root)
    hashes = {name: fingerprint(root) for name, root in sources.items()}
    assets = lambda values: {name: value for name, value in values.items() if name.startswith("Content/") and not name.startswith("Content/Engine/Shaders/")}
    if assets(hashes["baseline"]) != assets(hashes["candidate"]):
        raise ValueError("Baseline and candidate scene assets differ")
    report = {"original_baseline": original_baseline, "original_candidate": original_candidate, "source_hashes": hashes,
              "environment": environment(), "gates": GATES, "reference_spp": args.reference_spp, "runs": {}, "checks": {},
              "scope": "First-pass mirror denoising at transport resolution. Three seeds, a sixteen-frame pan, exact-term identity, conservative bypasses and a same-instance crease. Exact fixtures eliminate source diffuse cache transport and independently require zero nonexact raw channels before checking recomposition. The noisy fixture retains indirect light. Full-resolution fallback is outside this test."}
    common = ["--width", SIZE, "--height", SIZE, "--warmup", 0]
    labels = ["first"] + [f"pan{i}" for i in range(16)]
    images, masks = {}, None
    print("Evidence:", output, flush=True)
    try:
        cases = ["Noisy", "Folded", "Mapped", "Sharp", "Medium", "Emission", "ExactShadow", "MappedReceiver", "CurvedReceiver", "ReflectionView"]
        for case in cases:
            seeds = SEEDS if case == "Noisy" else [911]
            loaded = {}
            for seed in seeds:
                for name, root in sources.items():
                    folder = output / f"{case}-{name}-{seed}"
                    folder.mkdir()
                    sequence = folder / "events.txt"
                    events = ["at 0 capture first"]
                    if case == "Noisy":
                        for index in range(16):
                            events += [f'at {64 + index} set "Mirror denoise camera" Transform Position {(index + 1) * 0.03125} 0 4', f"at {64 + index} capture pan{index}"]
                    else:
                        events += ['at 1 set "Mirror denoise camera" Transform Position 0.03125 0 4', 'at 1 capture moved']
                    sequence.write_text("\n".join(events) + "\n")
                    scene = "Noisy" if case == "ReflectionView" else case
                    command = [f"Scenes/DIA/MirrorDenoise{scene}.scene"] + common + ["--frames", 80 if case == "Noisy" else 2,
                        "--seed", seed, "--sequence", sequence, "--capture-dir", folder]
                    if case == "ReflectionView":
                        command += ["--viewmode", "reflections"]
                    report["runs"][folder.name] = run(root / "Binaries/Release/Runtime.exe", command, folder / "runtime.log")
                    loaded[name, seed] = [capture(folder, label) for label in (labels if case == "Noisy" else ("first", "moved"))]
                for index, ((before, a), (after, b)) in enumerate(zip(loaded["baseline", seed], loaded["candidate", seed])):
                    if not all(np.array_equal(x, y) for x, y in zip(a, b)):
                        raise ValueError(f"Raw transport, exact terms or history moments changed: {case}, {seed}, {index}")
                    mirror = (a[3][..., 3] <= 0.02) & (a[0][..., 3] != 0) & (a[5][..., 3] == 1)
                    if case == "Folded":
                        report["checks"][f"crease_{index}"] = unchanged(before, after, crease_mask(a, a[5]), minimum=4)
                    elif case == "MappedReceiver":
                        report["checks"][f"MappedReceiver_{index}"] = mapped_receiver_check(before, after, a)
                    elif case != "Noisy":
                        report["checks"][f"{case}_{index}"] = unchanged(before, after, mirror)
                        if case in ("Mapped", "Sharp", "Medium", "Emission", "CurvedReceiver"):
                            report["checks"][f"{case}_metal_copy_{index}"] = {
                                "status": "PASS" if mirror.sum() >= 16 and np.array_equal(before[mirror], after[mirror]) else "FAIL",
                                "pixels": int(mirror.sum()), "scope": "Metal mirror RGB with mirror filtering bypassed."}
                    if case in ("ExactShadow", "Emission"):
                        source_plane = mirror & (np.abs(a[5][..., 2] - 5) < 1e-4)
                        for name, raw in (("baseline", a), ("candidate", b)):
                            for channel in (0, 1):
                                report["checks"][f"{case}_{name}_nonexact{channel}_{index}"] = unchanged(
                                    np.zeros_like(raw[channel][..., :3]), raw[channel][..., :3], source_plane)
                        report["checks"][f"{case}_exact_{index}"] = unchanged(a[4][..., :3], after, source_plane)
                        report["checks"][f"{case}_contrast_{index}"] = {"status": "PASS" if np.ptp(a[4][..., :3][mirror]) > 0.02 else "FAIL"}
            if case == "Noisy":
                for name in sources:
                    images[name] = np.array([[item[0] for item in loaded[name, seed]] for seed in SEEDS])
                masks = []
                for _, raw in loaded["baseline", SEEDS[0]]:
                    mask = (raw[0][..., 3] > 0) & (raw[1][..., 3] > 0) & (raw[3][..., 3] <= 0.02) & (np.abs(raw[5][..., 2] - 5) < 1e-4)
                    inside = np.zeros_like(mask)
                    inside[2:-2, 2:-2] = np.all(np.lib.stride_tricks.sliding_window_view(mask, (5, 5)), axis=(-2, -1))
                    masks.append(inside)
                masks = np.array(masks)
                before, after = [noise_metrics(images[name][:, 1:], masks[1:]) for name in sources]
                temporal = after["temporal_rms"] / max(before["temporal_rms"], 1e-12)
                spatial = after["spatial_rms"] / max(before["spatial_rms"], 1e-12)
                mean = abs(after["mean"] / max(before["mean"], 1e-12) - 1)
                report["checks"]["noise"] = {"status": "PASS" if temporal <= GATES["temporal_ratio"] and spatial <= GATES["spatial_ratio"] and mean <= GATES["mean_change"] else "FAIL",
                    "temporal_ratio": temporal, "spatial_ratio": spatial, "mean_change": mean, "baseline": before, "candidate": after}
                before, after = [noise_metrics(images[name][:, :1], masks[:1]) for name in sources]
                spatial = after["spatial_rms"] / max(before["spatial_rms"], 1e-12)
                mean = abs(after["mean"] / max(before["mean"], 1e-12) - 1)
                report["checks"]["startup"] = {"status": "PASS" if spatial <= GATES["spatial_ratio"] and mean <= GATES["mean_change"] else "FAIL",
                    "spatial_ratio": spatial, "mean_change": mean, "baseline": before, "candidate": after}
        refs = []
        for seed in (7919, 104729, 1299709):
            folder = output / f"reference-{seed}"
            folder.mkdir()
            sequence = folder / "events.txt"
            sequence.write_text('at 0 set "Mirror denoise camera" Transform Position 0.5 0 4\n')
            path = folder / "reference.pfm"
            report["runs"][folder.name] = run(baseline / "Binaries/Release/Runtime.exe", ["Scenes/DIA/MirrorDenoiseNoisy.scene"] + common + [
                "--frames", 1, "--seed", seed, "--sequence", sequence, "--capture-dir", folder,
                "--reference", path, "--reference-size", SIZE, "--reference-spp", args.reference_spp], folder / "runtime.log", timeout=max(120, args.reference_spp // 8))
            refs.append(read_pfm(path))
        refs = np.asarray(refs)
        if refs.shape != (3, SIZE, SIZE, 3) or not np.isfinite(refs).all():
            raise ValueError("Invalid CPU reference")
        before, after = [images[name][:, -1].mean(axis=0) for name in sources]
        report["checks"]["reference"] = reference_check(before, after, refs, np.ones((SIZE, SIZE), dtype=bool))
        report["checks"]["detail"] = gradient_check(before, after, refs)
        if any(fingerprint(root) != hashes[name] for name, root in sources.items()) or fingerprint(args.baseline) != original_baseline:
            raise ValueError("Frozen inputs changed")
        statuses = {check["status"] for check in report["checks"].values()}
        report["status"] = "FAIL" if "FAIL" in statuses else "BLOCKED" if "BLOCKED" in statuses else "PASS"
    except Exception as error:
        report["status"], report["failure"] = "FAIL", str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(report.get("status", "FAIL"), "Evidence:", output, flush=True)
    if report["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
