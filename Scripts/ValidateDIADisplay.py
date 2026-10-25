import argparse
import hashlib
import json
import math
import tempfile
import time
from pathlib import Path

import numpy as np

from ValidateDIA import ROOT, environment, fingerprint, freeze, read_pfm, run
from ValidateDIADynamics import read_rgba
from ValidateDIAMotion import noise_metrics


SEEDS = (911, 1237, 3217)
FRAMES = range(90, 102)


def sequence(frames, captures, speed=1):
    lines = []
    for frame in range(30, frames):
        t = (frame - 30) * speed
        lines += [f'at {frame} set "Main Camera" Transform Position {0.4 * math.sin(t * 2 * math.pi / 120):.9g} 0 5',
                  f'at {frame} set "Main Camera" Transform Rotation 0 {6 * math.sin(t * 2 * math.pi / 96):.9g} 0']
        if frame in captures:
            lines.append(f'at {frame} capture sample{frame}')
    return "\n".join(lines) + "\n"


def regions(ids, normals):
    assert ids.shape == (180, 240) and normals.shape == (180, 240, 4)
    neighbors = np.lib.stride_tricks.sliding_window_view(ids, (3, 3))
    same = np.zeros(ids.shape, dtype=bool)
    same[1:-1, 1:-1] = np.all(neighbors == ids[1:-1, 1:-1, None, None], axis=(-2, -1))
    curved = (ids > 0) & (normals[..., 3] > 0.02) & (normals[..., 3] < 0.5)
    edge = np.zeros(ids.shape, dtype=bool)
    edge[1:-1, 1:-1] = np.any(np.lib.stride_tricks.sliding_window_view(curved, (3, 3)), axis=(-2, -1)) & ~same[1:-1, 1:-1]
    masks = {"diffuse_interior": same & (ids > 0) & (normals[..., 3] >= 0.5),
             "glossy_interior": same & (ids > 0) & (normals[..., 3] < 0.5), "glossy_edge_band": edge}
    return {name: np.repeat(np.repeat(mask, 4, axis=0), 4, axis=1) for name, mask in masks.items()}


def preservation(before, after):
    half = before.astype(np.float16)
    lower = half.astype(np.float32) - np.nextafter(half, np.float16(-np.inf)).astype(np.float32)
    upper = np.nextafter(half, np.float16(np.inf)).astype(np.float32) - half.astype(np.float32)
    step = np.maximum(lower, np.where(np.isfinite(upper), upper, lower))
    error = np.abs(after - before)
    allowed = np.maximum(step, 0.000001)
    return {"status": "PASS" if np.all(error <= allowed) else "FAIL", "maximum_rgb_error": float(error.max()),
            "changed_components": int(np.count_nonzero(error)), "components_above_half_float_step": int(np.count_nonzero(error > allowed))}


def compact_capture_files(folder):
    folder = folder.resolve(strict=True)
    prefixes = {f"sample{frame}" for frame in FRAMES if frame != 95}
    files = []
    for path in sorted(folder.iterdir()):
        if path.is_file() and path.suffix in (".rgba", ".pfm") and path.name.split(".")[0] in prefixes:
            resolved = path.resolve(strict=True)
            assert not path.is_symlink() and resolved.parent == folder and resolved.name == path.name
            files.append(resolved)
    return files


def self_check():
    before = np.ones((4, 4, 3), dtype=np.float32)
    assert preservation(before, before)["status"] == "PASS"
    assert preservation(before, before + 0.01)["status"] == "FAIL"
    assert preservation(before, np.nextafter(before.astype(np.float16), np.float16(np.inf)).astype(np.float32))["status"] == "PASS"
    ids = np.ones((180, 240))
    normals = np.zeros((180, 240, 4)); normals[..., 3] = 0.7
    assert not regions(ids, normals)["glossy_edge_band"].any()
    ids[50:100, 80:130] = 2; normals[50:100, 80:130, 3] = 0.2
    masks = regions(ids, normals)
    assert masks["glossy_edge_band"].any() and masks["glossy_interior"][300, 400]
    events = sequence(102, FRAMES).splitlines()
    assert [int(line.split()[1]) for line in events] == sorted(int(line.split()[1]) for line in events)
    fast = sequence(102, FRAMES, 4).splitlines()
    assert [int(line.split()[1]) for line in fast] == [int(line.split()[1]) for line in events]
    assert fast != events
    with tempfile.TemporaryDirectory(prefix="dia-display-selection-") as temporary:
        root = Path(temporary).resolve()
        folder = root / "captures"
        folder.mkdir()
        for name in ("sample90.display.pfm", "sample101.raw1.rgba", "sample95.pfm", "sample95.raw0.rgba", "sample89.pfm", "sample90.json", "runtime.log"):
            (folder / name).write_bytes(b"selection test")
        (root / "sample90.pfm").write_bytes(b"outside capture folder")
        (folder / "nested").mkdir()
        (folder / "nested/sample90.pfm").write_bytes(b"nested capture")
        selected = compact_capture_files(folder)
        assert {path.name for path in selected} == {"sample90.display.pfm", "sample101.raw1.rgba"}
        assert all(path.parent == folder for path in selected)
        assert all(path.exists() for path in folder.glob("sample95.*"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--identical", action="store_true", help="Require every display component to remain within one half-float step.")
    parser.add_argument("--fast-motion", action="store_true", help="Move and turn four times faster along the same camera path.")
    parser.add_argument("--compact", action="store_true", help="Hash and remove validated capture payloads except sample95, retaining reports and source snapshots.")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("PASS display validation self-check")
        return
    if not args.baseline:
        parser.error("--baseline is required")
    baseline = args.baseline.resolve()
    speed = 4 if args.fast_motion else 1
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-display")
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    old_hashes = fingerprint(baseline)
    assets = lambda values: {k: v for k, v in values.items() if k.startswith("Content/") and not k.startswith("Content/Engine/Shaders/")}
    assert assets(hashes) == assets(old_hashes), "Scene assets differ"
    common = ["Scenes/DIA/Review.scene", "--width", 960, "--height", 720, "--internal-height", 180]
    paths = {"baseline": baseline, "candidate": frozen}
    report = {"source_hashes": hashes, "baseline_hashes": old_hashes, "environment": environment(), "checks": {}, "runs": {},
              "settings": {"display": [960, 720], "internal": [240, 180], "seeds": SEEDS, "captures": list(FRAMES),
                           "translation_metres": 0.4, "yaw_degrees": 6, "motion_speed_multiplier": speed,
                           "timing_pairs": 3, "identical": args.identical},
              "scope": "Three seeds during continuous translation and rotation. Edge bands conservatively expand low-resolution instance boundaries and include both sides of the silhouette. Timings exclude captures and use CPU submission and wait, not GPU timestamps."}
    if args.compact:
        report["compact_scope"] = "After each completed quality folder passes existing capture checks and its images are loaded into memory, remove only its sample90 through sample101 PFM and RGBA payloads except every sample95 file. Preserve reports, logs, metadata and frozen sources. Per-file SHA256 and byte sizes are saved before removal."
        report["compact_captures"] = {}
    try:
        report["startup_preparation"] = {}
        for name, source in paths.items():
            folder = output / f"startup-{name}"
            folder.mkdir()
            report["startup_preparation"][name] = run(source / "Binaries/Release/Runtime.exe", common + [
                "--frames", 1, "--warmup", 0, "--seed", 911], folder / "runtime.log", timeout=240)
        quality = output / "quality.events"; quality.write_text(sequence(102, FRAMES, speed))
        geometry, images = None, {name: [] for name in paths}
        for index, seed in enumerate(SEEDS):
            for name in (("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")):
                folder = output / f"{name}-{seed}"; folder.mkdir()
                print(name, seed, "display motion", flush=True)
                report["runs"][folder.name] = run(paths[name] / "Binaries/Release/Runtime.exe", common + [
                    "--frames", 102, "--warmup", 0, "--seed", seed, "--sequence", quality, "--capture-dir", folder], folder / "runtime.log", timeout=240)
                captured, guides = [], []
                for frame in FRAMES:
                    prefix = folder / f"sample{frame}"
                    image = read_pfm(str(prefix) + ".display.pfm")
                    assert image.shape == (720, 960, 3) and np.isfinite(image).all()
                    assert not np.any(np.all(image == [1, 0, 1], axis=-1)), "Resolve diagnostics in display output"
                    assert np.all(read_rgba(str(prefix) + ".raw4.rgba")[..., 3] == 0)
                    guides.append((read_rgba(str(prefix) + ".raw0.rgba")[..., 3],
                                   read_rgba(str(prefix) + ".raw3.rgba"), read_rgba(str(prefix) + ".raw2.rgba")[..., :3]))
                    captured.append(image)
                if geometry is None:
                    geometry = guides
                else:
                    assert all(all(np.array_equal(a, b) for a, b in zip(old, new)) for old, new in zip(geometry, guides)), "Primary geometry differs"
                images[name].append(np.asarray(captured))
                if args.compact:
                    files = compact_capture_files(folder)
                    manifest = {path.name: {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "bytes": path.stat().st_size} for path in files}
                    report["compact_captures"][folder.name] = manifest
                    (folder / "compact-captures.json").write_text(json.dumps(manifest, indent=2) + "\n")
                    for path in files:
                        path.unlink()
        images = {name: np.asarray(values) for name, values in images.items()}
        for name in regions(geometry[0][0], geometry[0][1]):
            mask = np.asarray([regions(ids, normals)[name] for ids, normals, _ in geometry])
            before, after = [noise_metrics(images[label], mask) for label in paths]
            spatial = after["spatial_rms"] / max(before["spatial_rms"], 1e-12)
            temporal = after["temporal_rms"] / max(before["temporal_rms"], 1e-12)
            mean = abs(after["mean"] / max(before["mean"], 1e-12) - 1)
            report["checks"][name] = {"status": "PASS" if max(spatial, temporal) <= 1.02 and mean <= 0.03 else "FAIL",
                                      "spatial_rms_ratio": spatial, "temporal_rms_ratio": temporal, "mean_change": mean,
                                      "baseline": before, "candidate": after}
        if args.identical:
            report["checks"]["display_preservation"] = preservation(images["baseline"], images["candidate"])
        timing = output / "timing.events"; timing.write_text(sequence(180, (), speed))
        medians = {name: [] for name in paths}
        for pair in range(3):
            for name in (("candidate", "baseline") if pair % 2 == 0 else ("baseline", "candidate")):
                folder = output / f"timing-{name}-{pair}"; folder.mkdir()
                print(name, pair, "timing", flush=True)
                report["runs"][folder.name] = run(paths[name] / "Binaries/Release/Runtime.exe", common + [
                    "--frames", 180, "--warmup", 60, "--seed", 911, "--sequence", timing, "--capture-dir", folder,
                    "--metrics", folder / "metrics.json"], folder / "runtime.log", timeout=240)
                metrics = json.loads((folder / "metrics.json").read_text())
                assert metrics["lighting_resident_bytes_bound"] <= 256 * 1024 * 1024 and metrics["lighting_scene_uploads"] == 1
                medians[name].append(metrics["cpu_submission_wait_ms"]["p50"])
        ratio = float(np.median(medians["candidate"]) / np.median(medians["baseline"]))
        report["checks"]["frame_time"] = {"status": "PASS" if ratio <= 0.8 else "FAIL", "median_ratio": ratio, "pairs_ms": medians}
        assert fingerprint(frozen) == hashes and fingerprint(baseline) == old_hashes
        report["status"] = "PASS" if all(check["status"] == "PASS" for check in report["checks"].values()) else "FAIL"
    except Exception as error:
        report["status"], report["failure"] = "FAIL", str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(report.get("status", "FAIL"), "Evidence:", output, flush=True)
    if report["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
