import argparse
import hashlib
import json
import tempfile
import time
from pathlib import Path

import numpy as np

from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, run
from ValidateDIADynamics import read_rgba
from ValidateDIASpeckles import receiver_mask


SEEDS = [911, 1237, 3217]
REFERENCE_SEEDS = [7919, 104729, 1299709]
POSES = [3, 12, 23]
SIZE = 96
GATES = {"temporal_rms_ratio": 0.75, "spatial_rms_ratio": 1.0, "mean_change": 0.03,
         "reference_error_ratio": 1.05, "reference_relative_uncertainty": 0.1,
         "minimum_noise_pixels_per_frame": 64, "minimum_reference_pixels": 16}


def reference_paths(evidence, report, target):
    keys = ("extent", "internal_height", "reference_poses", "reference_seeds", "reference_spp", "step_metres")
    if report["baseline_hashes"] != target["baseline_hashes"] or any(report["settings"][key] != target["settings"][key] for key in keys):
        raise ValueError("Reference baseline or settings differ")
    if report.get("reference_source_hashes", report["baseline_hashes"]) != target.get("reference_source_hashes", target["baseline_hashes"]):
        raise ValueError("Reference source identity differs")
    origin = Path(report.get("reference_origin", evidence)).resolve()
    paths = {}
    for pose in target["settings"]["reference_poses"]:
        for seed in target["settings"]["reference_seeds"]:
            name = f"reference-{pose}-{seed}/reference.pfm"
            path = origin / name
            expected = report.get("reference_hashes", {}).get(name)
            if expected is None or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Missing or changed reference hash: {path}")
            paths[pose, seed] = path
    return origin, paths


def noise_metrics(images, mask):
    if not np.isfinite(images).all() or np.any(mask.sum(axis=(1, 2)) < GATES["minimum_noise_pixels_per_frame"]):
        raise ValueError("Nonfinite image or insufficient noise pixels in a frame")
    light = luminance(images)
    residual = light - light.mean(axis=0)
    power = float(np.mean(residual[:, mask] ** 2) * len(images) / (len(images) - 1))
    pairs = mask[1:] & mask[:-1]
    increment = np.diff(residual, axis=1)
    temporal = float(np.mean(increment[:, pairs] ** 2) * len(images) / (len(images) - 1)) if pairs.any() else None
    return {"mean": float(light[:, mask].mean()), "spatial_rms": float(np.sqrt(power)),
            "temporal_rms": float(np.sqrt(temporal)) if temporal is not None else None,
            "pixels_per_frame": mask.sum(axis=(1, 2)).tolist()}


def noise_check(before, after, moving=True):
    spatial = after["spatial_rms"] / max(before["spatial_rms"], 1e-12)
    temporal = after["temporal_rms"] / max(before["temporal_rms"], 1e-12) if moving else None
    change = abs(after["mean"] / max(before["mean"], 1e-12) - 1)
    enough = min(before["pixels_per_frame"] + after["pixels_per_frame"]) >= GATES["minimum_noise_pixels_per_frame"]
    passed = enough and spatial <= GATES["spatial_rms_ratio"] and change <= GATES["mean_change"]
    passed &= not moving or temporal <= GATES["temporal_rms_ratio"]
    return {"status": "PASS" if passed else "FAIL", "spatial_rms_ratio": spatial,
            "temporal_rms_ratio": temporal, "mean_change": change, "baseline": before, "candidate": after}


def reference_check(before, after, references, mask):
    reference = references.mean(axis=0)
    mean = max(float(luminance(reference)[mask].mean()), 1e-5) if mask.any() else 1e-5
    errors = [float(np.sqrt(np.mean((image[mask] - reference[mask]) ** 2))) if mask.any() else None
              for image in (before, after)]
    uncertainty = float(np.sqrt(np.mean(np.var(references[:, mask], axis=0, ddof=1) / len(references)))) / mean if mask.any() else None
    enough = int(mask.sum()) >= GATES["minimum_reference_pixels"]
    passed = enough and errors[1] <= errors[0] * GATES["reference_error_ratio"] + 1e-6
    status = "FAIL" if not passed else "BLOCKED" if uncertainty > GATES["reference_relative_uncertainty"] else "PASS"
    return {"status": status, "pixels": int(mask.sum()), "baseline_rmse": errors[0], "candidate_rmse": errors[1],
            "reference_relative_uncertainty": uncertainty, "reference_mean_luminance": mean}


def gradient_check(before, after, references):
    reference = references.mean(axis=0)
    gradients = lambda image: np.concatenate((np.diff(image, axis=0).reshape(-1, 3), np.diff(image, axis=1).reshape(-1, 3)))
    target = gradients(reference)
    magnitude = np.linalg.norm(target, axis=-1)
    selected = magnitude >= max(0.02, float(np.quantile(magnitude, 0.9)))
    errors = [float(np.sqrt(np.mean((gradients(image)[selected] - target[selected]) ** 2))) if selected.any() else None for image in (before, after)]
    return {"status": "PASS" if selected.sum() >= 16 and errors[1] <= errors[0] * GATES["reference_error_ratio"] + 1e-6 else "FAIL",
            "edge_pairs": int(selected.sum()), "baseline_gradient_rmse": errors[0], "candidate_gradient_rmse": errors[1],
            "reference_gradient_threshold": max(0.02, float(np.quantile(magnitude, 0.9)))}


def revealed_mask(position, ids, old_ids, old_x):
    depth = 5 - position[..., 2]
    scale = np.tan(np.deg2rad(55 / 2))
    safe_depth = np.maximum(depth, 1e-6)
    x = np.floor(((position[..., 0] - old_x) / (safe_depth * scale) * 0.5 + 0.5) * SIZE).astype(int)
    y = np.floor((0.5 - position[..., 1] / (safe_depth * scale) * 0.5) * SIZE).astype(int)
    inside = (depth > 0) & (x >= 0) & (x < SIZE) & (y >= 0) & (y < SIZE)
    previous = old_ids[np.clip(y, 0, SIZE - 1), np.clip(x, 0, SIZE - 1)]
    return inside & (ids != 0) & (np.abs(ids) != np.abs(previous))


def self_check():
    rng = np.random.default_rng(11)
    mask = np.ones((8, 16, 16), dtype=bool)
    noise = rng.normal(0, 0.1, (3, 8, 16, 16, 3))
    before = noise_metrics(1 + noise, mask)
    moving_signal = np.arange(8)[None, :, None, None, None] * 0.1
    assert np.isclose(before["temporal_rms"], noise_metrics(1 + noise + moving_signal, mask)["temporal_rms"])
    assert noise_check(before, noise_metrics(1 + noise * 0.5, mask))["status"] == "PASS"
    assert noise_check(before, before)["status"] == "FAIL"
    assert noise_check(before, noise_metrics(0.8 + noise * 0.1, mask))["status"] == "FAIL"
    edge = np.zeros((16, 16, 3))
    edge[:, 8:] = 1
    references = np.stack([edge] * 3)
    shifted = np.roll(edge, 1, axis=1)
    all_pixels = np.ones((16, 16), dtype=bool)
    assert reference_check(edge, edge, references, all_pixels)["status"] == "PASS"
    assert reference_check(edge, shifted, references, all_pixels)["status"] == "FAIL"
    assert gradient_check(edge, (edge + shifted) / 2, references)["status"] == "FAIL"
    assert reference_check(edge, edge, references, all_pixels & False)["status"] == "FAIL"
    assert gradient_check(edge * 0, edge * 0, references * 0)["status"] == "FAIL"
    try:
        noise_metrics(1 + noise, mask & False)
    except ValueError:
        pass
    else:
        raise AssertionError("Empty noise masks must fail")
    y, x = np.indices((SIZE, SIZE))
    positions = np.zeros((SIZE, SIZE, 3))
    positions[..., 0] = ((x + 0.5) / SIZE * 2 - 1) * 5 * np.tan(np.deg2rad(55 / 2))
    positions[..., 1] = (1 - (y + 0.5) / SIZE * 2) * 5 * np.tan(np.deg2rad(55 / 2))
    ids = np.ones((SIZE, SIZE))
    assert not revealed_mask(positions, ids, ids, 0).any()
    old_ids = ids.copy()
    old_ids[32:64, 32:64] = 2
    assert np.array_equal(revealed_mask(positions, ids, old_ids, 0), old_ids == 2)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        path = root / "reference-3-7919/reference.pfm"
        path.parent.mkdir()
        path.write_bytes(b"reference fixture")
        report = {"baseline_hashes": {"Runtime": "frozen"}, "settings": {
            "extent": [96, 96], "internal_height": 96, "reference_poses": [3],
            "reference_seeds": [7919], "reference_spp": 8192, "step_metres": 0.02},
            "reference_hashes": {"reference-3-7919/reference.pfm": hashlib.sha256(path.read_bytes()).hexdigest()}}
        assert reference_paths(root, report, report)[1][3, 7919] == path
        explicit = {**report, "reference_source_hashes": dict(report["baseline_hashes"])}
        assert reference_paths(root, report, explicit)[1][3, 7919] == path
        assert reference_paths(root, explicit, report)[1][3, 7919] == path
        for field in ("baseline_hashes", "settings", "reference_hashes", "reference_source_hashes"):
            changed = json.loads(json.dumps(report))
            if field == "settings":
                changed[field]["reference_spp"] = 512
            else:
                changed[field] = {}
            try:
                reference_paths(root, changed, report)
            except ValueError:
                pass
            else:
                raise AssertionError("Unverified references must fail")
        changed = {**explicit, "reference_source_hashes": {"Runtime": "different reference renderer"}}
        try:
            reference_paths(root, explicit, changed)
        except ValueError:
            pass
        else:
            raise AssertionError("Different target reference renderer must fail")
        path.write_bytes(b"changed reference")
        try:
            reference_paths(root, report, report)
        except ValueError:
            pass
        else:
            raise AssertionError("Changed reference contents must fail")


def load_capture(folder, label):
    prefix = folder / label
    image = read_pfm(str(prefix) + ".pfm")
    ids = read_rgba(str(prefix) + ".raw0.rgba")[..., 3]
    normals = read_rgba(str(prefix) + ".raw3.rgba")
    positions = read_rgba(str(prefix) + ".raw2.rgba")[..., :3]
    diagnostics = read_rgba(str(prefix) + ".raw4.rgba")[..., 3]
    if image.shape != (SIZE, SIZE, 3) or np.any(diagnostics != 0):
        raise ValueError(f"Invalid extent or query diagnostics at {prefix}")
    return image, ids, normals, positions


def combined_status(checks):
    statuses = {check["status"] for check in checks.values()}
    return "FAIL" if "FAIL" in statuses else "BLOCKED" if "BLOCKED" in statuses else "PASS"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--stage", choices=("pan", "reference", "all"), default="all")
    parser.add_argument("--reference-spp", type=int, default=8192, help="Samples for new evidence. Reference stage uses the recorded setting.")
    parser.add_argument("--reference-root", type=Path, help="Frozen renderer for CPU references. Defaults to baseline for new evidence and the recorded source for the reference stage.")
    parser.add_argument("--reference-evidence", type=Path, help="Reuse recorded CPU references after checking their baseline, settings and file hashes.")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("Motion metric self-check PASS")
        return
    if args.stage != "reference" and not 1 <= args.reference_spp <= 16384:
        parser.error("--reference-spp must be between 1 and 16384")
    if args.stage == "pan" and args.reference_evidence:
        parser.error("--reference-evidence requires the reference or all stage")
    if args.stage == "reference":
        if args.evidence is None:
            parser.error("--evidence is required for the reference stage")
        output = args.evidence.resolve()
        report = json.loads((output / "results.json").read_text())
        baseline, frozen = Path(report["baseline"]), output / "frozen"
        reference_root = Path(report.get("reference_root", baseline)).resolve()
        if args.reference_root and args.reference_root.resolve() != reference_root:
            parser.error("--reference-root differs from the source recorded in the evidence")
        report.setdefault("reference_root", str(reference_root))
        report.setdefault("reference_source_hashes", dict(report["baseline_hashes"]))
    else:
        if args.baseline is None:
            parser.error("--baseline is required for pan captures")
        baseline = args.baseline.resolve()
        output = args.evidence.resolve() if args.evidence else ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-motion")
        output.mkdir(parents=True, exist_ok=False)
        frozen, hashes = freeze(output)
        report = {"source_hashes": hashes, "baseline_hashes": fingerprint(baseline), "baseline": str(baseline),
                  "environment": environment(), "gates": GATES, "runs": {}, "checks": {},
                  "settings": {"extent": [SIZE, SIZE], "internal_height": SIZE, "seeds": SEEDS,
                               "startup_frames": [0, 7], "pan_frames": [400, 423], "step_metres": 0.02,
                               "reference_poses": POSES, "reference_seeds": REFERENCE_SEEDS, "reference_spp": args.reference_spp},
                  "scope": "Seed-demeaned temporal increments remove deterministic camera motion, not bias or stale history. Independent CPU references at three poses constrain spatial error, strong edges and newly visible receivers. Three render seeds and three reference seeds provide limited uncertainty. This square test matches CPU pixel centers and does not test full resolution or arbitrary motion.",
                  "mask": "Noise uses the full image with a 5 by 5 matching receiver neighborhood, excluding signed mapped receivers. Rough receivers also require aligned normals. Glossy receivers retain curved interiors. Reference spatial error and edges use all pixels. Revealed receivers compare projected identity against four frames earlier, excluding only out-of-view and sky pixels."}
        reference_root = (args.reference_root or baseline).resolve()
        report["reference_root"] = str(reference_root)
        report["reference_source_hashes"] = fingerprint(reference_root)
    sources = {"baseline": baseline, "candidate": frozen}
    def verify():
        if fingerprint(baseline) != report["baseline_hashes"] or fingerprint(frozen) != report["source_hashes"] or fingerprint(reference_root) != report["reference_source_hashes"]:
            raise ValueError("Frozen inputs changed")
    verify()
    assets = lambda hashes: {name: value for name, value in hashes.items() if name.startswith("Content/") and not name.startswith("Content/Engine/Shaders/")}
    if assets(report["baseline_hashes"]) != assets(report["source_hashes"]):
        raise ValueError("Baseline and candidate scene assets differ")
    if assets(report["reference_source_hashes"]) != assets(report["source_hashes"]):
        raise ValueError("Reference source and candidate scene assets differ")
    common = ["Scenes/DIA/Review.scene", "--width", SIZE, "--height", SIZE, "--internal-height", SIZE, "--warmup", 0]
    labels = ["startup0", "startup7", "before"] + [f"pan{index}" for index in range(24)]
    print("Evidence:", output, flush=True)
    try:
        reused = None
        if args.reference_evidence:
            origin_report = json.loads((args.reference_evidence / "results.json").read_text())
            origin, reused = reference_paths(args.reference_evidence, origin_report, report)
            report["reference_origin"] = str(origin)
            report["reference_source_report"] = str(args.reference_evidence.resolve() / "results.json")
            report["reference_hashes"] = dict(origin_report["reference_hashes"])
        if args.stage != "reference":
            events = ['at 0 capture startup0', 'at 7 capture startup7', 'at 399 capture before']
            for index in range(24):
                events += [f'at {400 + index} set "Main Camera" Transform Position {(index + 1) * 0.02} 0 5',
                           f'at {400 + index} capture pan{index}']
            sequence = output / "events.txt"
            sequence.write_text("\n".join(events) + "\n")
            for index, seed in enumerate(SEEDS):
                for name in (("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")):
                    folder = output / f"{name}-{seed}"
                    folder.mkdir()
                    print(name, seed, "pan", flush=True)
                    report["runs"][folder.name] = run(sources[name] / "Binaries/Release/Runtime.exe", common + [
                        "--frames", 424, "--seed", seed, "--sequence", sequence, "--capture-dir", folder], folder / "runtime.log")
        images, geometry = {}, None
        for name in sources:
            rendered = []
            for seed in SEEDS:
                captured = [load_capture(output / f"{name}-{seed}", label) for label in labels]
                if geometry is None:
                    geometry = [(item[1], item[2], item[3]) for item in captured]
                elif any(not all(np.array_equal(a, b) for a, b in zip(expected, item[1:])) for expected, item in zip(geometry, captured)):
                    raise ValueError("Primary geometry differs across seeds or renderers")
                rendered.append([item[0] for item in captured])
            images[name] = np.asarray(rendered)
        masks = []
        for ids, normals, _ in geometry:
            eligible = normals.copy()
            eligible[..., 3] = 1
            rough = receiver_mask(ids, eligible)
            eligible[..., :3] = [0, 0, 1]
            curved = receiver_mask(ids, eligible)
            masks.append(np.where(normals[..., 3] >= 0.5, rough, curved))
        masks = np.asarray(masks)
        roughness = np.asarray([item[1][..., 3] for item in geometry])
        for group, selected in (("rough", masks & (roughness >= 0.5)), ("glossy", masks & (roughness < 0.5))):
            for stage, indices in (("startup", slice(0, 2)), ("pan", slice(3, None))):
                measured = [noise_metrics(images[name][:, indices], selected[indices]) for name in sources]
                report["checks"][f"{stage}_{group}"] = noise_check(*measured, moving=stage == "pan")
        if args.stage != "pan":
            if reused is None:
                report.pop("reference_origin", None)
                report.pop("reference_source_report", None)
                report["reference_hashes"] = {}
            for pose in POSES:
                refs = []
                for seed in REFERENCE_SEEDS:
                    if reused is not None:
                        refs.append(read_pfm(reused[pose, seed]))
                        continue
                    folder = output / f"reference-{pose}-{seed}"
                    folder.mkdir(exist_ok=True)
                    sequence = folder / "events.txt"
                    sequence.write_text(f'at 0 set "Main Camera" Transform Position {(pose + 1) * 0.02} 0 5\n')
                    path = folder / "reference.pfm"
                    print("reference", pose, seed, flush=True)
                    report["runs"][folder.name] = run(reference_root / "Binaries/Release/Runtime.exe", common + [
                        "--frames", 1, "--seed", seed, "--sequence", sequence, "--capture-dir", folder,
                        "--reference", path, "--reference-size", SIZE, "--reference-spp", report["settings"]["reference_spp"]], folder / "runtime.log",
                        timeout=max(120, report["settings"]["reference_spp"] // 8))
                    report.setdefault("reference_hashes", {})[path.relative_to(output).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
                    refs.append(read_pfm(path))
                refs = np.asarray(refs)
                if refs.shape != (len(REFERENCE_SEEDS), SIZE, SIZE, 3) or not np.isfinite(refs).all():
                    raise ValueError(f"Invalid reference image at pose {pose}")
                index, previous = pose + 3, max(2, pose - 1)
                before, after = [images[name][:, index].mean(axis=0) for name in sources]
                full = np.ones((SIZE, SIZE), dtype=bool)
                report["checks"][f"pose{pose}_spatial"] = reference_check(before, after, refs, full)
                report["checks"][f"pose{pose}_detail"] = gradient_check(before, after, refs)
                old_x = 0 if previous == 2 else (previous - 2) * 0.02
                reveal = revealed_mask(geometry[index][2], geometry[index][0], geometry[previous][0], old_x)
                report["checks"][f"pose{pose}_revealed"] = reference_check(before, after, refs, reveal)
        verify()
        if reused is not None:
            reference_paths(args.reference_evidence, origin_report, report)
        elif args.stage != "pan":
            reference_paths(output, report, report)
        report["status"] = combined_status(report["checks"])
        if args.stage == "pan" and report["status"] == "PASS":
            report["status"] = "PENDING_REFERENCE"
    except Exception as error:
        report["status"], report["failure"] = "FAIL", str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(report.get("status", "FAIL"), "Evidence:", output, flush=True)
    if report["status"] not in ("PASS", "PENDING_REFERENCE"):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
