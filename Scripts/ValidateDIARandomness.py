import argparse
import json
import time
from pathlib import Path

import numpy as np

from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, region, run
from ValidateDIADynamics import read_rgba
from ValidateDIASpeckles import receiver_mask


REGIONS = {
    "left_wall": [0.01, 0.3, 0.19, 0.8],
    "right_wall": [0.81, 0.3, 0.99, 0.8],
    "floor": [0.1, 0.9, 0.5, 0.98],
    "back_wall": [0.25, 0.28, 0.36, 0.5],
}


def correlation(a, b):
    a, b = a.ravel(), b.ravel()
    if len(a) < 4096:
        return {"status": "BLOCKED", "samples": len(a), "correlation": None}
    a, b = a - a.mean(), b - b.mean()
    denominator = float(np.sqrt(np.dot(a, a) * np.dot(b, b)))
    value = float(np.dot(a, b) / denominator) if denominator > 1e-20 else None
    return {
        "status": "BLOCKED" if value is None else "PASS" if abs(value) < 0.05 else "FAIL",
        "samples": len(a), "correlation": value,
        "nominal_iid_95_half_width": float(1.96 / np.sqrt(len(a))),
    }


def statistics(light, mask, identities):
    residual = light - light.mean(axis=0)
    correlations = {}
    for offset in range(1, 5):
        for axis, name in ((1, "y"), (2, "x")):
            left = [slice(None)] * 3
            right = left.copy()
            left[axis], right[axis] = slice(None, -offset), slice(offset, None)
            first, second = tuple(left), tuple(right)
            pair_mask = mask[first[1:]] & mask[second[1:]]
            pair_mask &= identities[first[1:]] == identities[second[1:]]
            correlations[f"{name}_{offset}"] = correlation(residual[first][:, pair_mask], residual[second][:, pair_mask])
    correlations["temporal_lag_1"] = correlation(residual[:-1, mask], residual[1:, mask])
    return {
        "pixels": int(np.count_nonzero(mask)),
        "mean": float(light[:, mask].mean()) if mask.any() else None,
        "temporal_variance": float(np.var(light[:, mask], axis=0, ddof=1).mean()) if mask.any() else None,
        "correlations": correlations,
    }


def compare(before, after):
    variance = before["temporal_variance"]
    ratio = after["temporal_variance"] / variance if variance and after["temporal_variance"] is not None else None
    statuses = {entry["status"] for entry in after["correlations"].values()}
    statuses.add("BLOCKED" if ratio is None else "PASS" if ratio <= 1.02 else "FAIL")
    if min(before["pixels"], after["pixels"]) < 64:
        statuses.add("BLOCKED")
    return {
        "status": "FAIL" if "FAIL" in statuses else "BLOCKED" if "BLOCKED" in statuses else "PASS",
        "variance_ratio": ratio,
        "mean_change_fraction": after["mean"] / before["mean"] - 1 if before["mean"] else None,
        "baseline": before, "candidate": after,
    }


def self_check():
    rng = np.random.default_rng(911)
    independent = rng.normal(size=(64, 32, 32))
    mask = np.ones((32, 32), dtype=bool)
    identities = np.ones((32, 32))
    measured = statistics(independent, mask, identities)
    assert compare(measured, measured)["status"] == "PASS"
    assert compare(measured, statistics(independent * 1.1, mask, identities))["status"] == "FAIL"
    shared = np.broadcast_to(rng.normal(size=(64, 1, 1)), independent.shape)
    assert statistics(shared, mask, identities)["correlations"]["x_1"]["correlation"] > 0.99
    for frame in range(1, len(independent)):
        independent[frame] += 0.8 * independent[frame - 1]
    assert statistics(independent, mask, identities)["correlations"]["temporal_lag_1"]["correlation"] > 0.6
    assert correlation(np.ones(64), np.ones(64))["status"] == "BLOCKED"


def load_sequence(folder):
    images = []
    mask = None
    identities = None
    for frame in range(128, 192):
        prefix = folder / f"f{frame}"
        raw = read_rgba(str(prefix) + ".raw0.rgba")
        normals = read_rgba(str(prefix) + ".raw3.rgba")
        selected = receiver_mask(raw[..., 3], normals)
        if identities is None:
            identities = raw[..., 3]
        else:
            selected &= raw[..., 3] == identities
        mask = selected if mask is None else mask & selected
        images.append(luminance(raw[..., :3]))
    return np.stack(images), mask, identities


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--seed", type=int, choices=(911, 1237, 3217), default=911)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("Randomness metric self-check PASS")
        return
    if args.baseline is None:
        parser.error("--baseline is required for GPU comparison")
    baseline = args.baseline.resolve()
    baseline_hashes = fingerprint(baseline)
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-randomness")
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    report = {
        "source_hashes": hashes, "baseline_hashes": baseline_hashes, "environment": environment(),
        "settings": {"extent": [128, 96], "internal_height": 96, "warmup": 128, "captured_frames": [128, 191], "seed": args.seed, "regions": REGIONS},
        "gates": {"variance_ratio": 1.02, "absolute_correlation": 0.05, "minimum_pixels": 64, "minimum_correlation_samples": 4096},
        "uncertainty": "Correlation intervals assume independent samples and are descriptive only. Overlapping pairs and shared cache lighting can increase dependence. Temporal demeaning over 64 frames also introduces a small negative lag correlation under an IID null.",
        "runs": {}, "regions": {}, "checks": {},
    }
    common = ["Scenes/DIA/Review.scene", "--width", 128, "--height", 96,
              "--internal-height", 96, "--frames", 192, "--warmup", 128]
    print("Evidence:", output, flush=True)
    try:
        sequences = {}
        for label, source in (("baseline", baseline), ("candidate", frozen)):
            folder = output / label
            folder.mkdir()
            sequence = folder / "events.txt"
            sequence.write_text("\n".join(f"at {frame} capture f{frame}" for frame in range(128, 192)) + "\n")
            print(label, "raw samples", flush=True)
            report["runs"][label] = run(source / "Binaries/Release/Runtime.exe", common + [
                "--seed", args.seed, "--sequence", sequence, "--capture-dir", folder,
            ], folder / "runtime.log")
            sequences[label] = load_sequence(folder)
        before, after = sequences["baseline"], sequences["candidate"]
        mask = before[1] & after[1] & (before[2] == after[2])
        for name, bounds in REGIONS.items():
            selected = region(mask, bounds)
            identity = region(before[2], bounds)
            values = []
            for sequence in (before, after):
                light = np.stack([region(frame, bounds) for frame in sequence[0]])
                values.append(statistics(light, selected, identity))
            report["regions"][name] = compare(*values)
        original = read_pfm(output / "candidate/f191.pfm")
        alternate_seed = 1237 if args.seed != 1237 else 3217
        for label, seed in (("duplicate", args.seed), ("alternate", alternate_seed)):
            if label == "duplicate":
                folder = output / label
                folder.mkdir()
                path = folder / "f191.pfm"
                arguments = common + ["--seed", seed, "--sequence", output / "candidate/events.txt", "--capture-dir", folder]
                scope = "Bitwise replay on this device with the same seed, frame count and diagnostic capture sequence."
            else:
                path = output / f"{label}.pfm"
                arguments = common + ["--seed", seed, "--hdr", path]
                scope = "A different seed changes the final HDR image."
            report["runs"][label] = run(frozen / "Binaries/Release/Runtime.exe", arguments, output / f"{label}.log")
            image = read_pfm(path)
            maximum = float(np.max(np.abs(image - original)))
            passed = np.array_equal(image, original) if label == "duplicate" else maximum > 1e-6
            report["checks"][label] = {"status": "PASS" if passed else "FAIL", "maximum_absolute_difference": maximum, "scope": scope}
        assert fingerprint(frozen) == hashes and fingerprint(baseline) == baseline_hashes
        statuses = {entry["status"] for entry in report["regions"].values()} | {entry["status"] for entry in report["checks"].values()}
        report["status"] = "FAIL" if "FAIL" in statuses else "BLOCKED" if "BLOCKED" in statuses else "PASS"
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
