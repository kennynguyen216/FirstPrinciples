import argparse
import hashlib
import json
import time
from pathlib import Path

import numpy as np

from ValidateDIA import ROOT, environment, freeze, fingerprint, read_pfm, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=ROOT / "Intermediate/Baseline/Binaries/Release/Runtime.exe")
    args = parser.parse_args()
    if not args.baseline.is_file():
        parser.error("Build the instrumented 75e5fb7 baseline first")
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-performance")
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    candidate = frozen / "Binaries/Release/Runtime.exe"
    baseline = args.baseline.resolve()
    baseline_root = baseline.parents[2]
    def baseline_fingerprint():
        files = [baseline, baseline.with_name("SDL3.dll")]
        if baseline.with_name("GameScripts.dll").is_file(): files.append(baseline.with_name("GameScripts.dll"))
        files.extend(baseline_root.glob("*.fpproject"))
        for folder in ("src", "Game", "Source", "Scripts", "Content", "Engine/Content"):
            files.extend(path for path in (baseline_root / folder).rglob("*") if path.is_file() and "__pycache__" not in path.parts)
        return {path.relative_to(baseline_root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(set(files))}
    baseline_hashes = baseline_fingerprint()
    report = {"environment": environment(), "candidate_hashes": hashes,
              "baseline_executable_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "baseline_hashes": baseline_hashes,
              "baseline_revision": "75e5fb7", "baseline_instrumentation": "HDR readback and fixed-frame timing only",
              "gpu_critical_ms": {"status": "BLOCKED", "reason": "SDL_GPU has no timestamp query API"},
              "pairs": {}, "hdr_identity": {}, "independent_review": "PENDING"}
    common = ["--frames", 600, "--warmup", 300, "--width", 1280, "--height", 720]
    try:
        for fixture in ("Scenes/Main.scene", "Scenes/DIA/Point.scene"):
            label = Path(fixture).stem
            a, b = output / f"{label}-baseline.pfm", output / f"{label}-disabled.pfm"
            run(baseline, [fixture, *common, "--hdr", a, "--metrics", output / f"{label}-baseline.json"], output / f"{label}-baseline.log")
            run(candidate, [fixture, *common, "--dia", 0, "--hdr", b, "--metrics", output / f"{label}-disabled.json"], output / f"{label}-disabled.log")
            difference = float(np.max(np.abs(read_pfm(a) - read_pfm(b))))
            report["hdr_identity"][fixture] = {"status": "PASS" if difference == 0 else "FAIL", "max_absolute_difference": difference}
        for enabled in (False, True):
            results = []
            for pair in range(3):
                print(f"Pair {pair + 1}/3, DIA {'enabled' if enabled else 'disabled'}: baseline then candidate", flush=True)
                prefix = output / f"{'enabled' if enabled else 'disabled'}-{pair}"
                a, b = Path(str(prefix) + ".baseline.json"), Path(str(prefix) + ".candidate.json")
                baseline_run = run(baseline, ["Scenes/DIA/Review.scene", *common, "--metrics", a], Path(str(prefix) + ".baseline.log"))
                candidate_run = run(candidate, ["Scenes/DIA/Review.scene", *common, "--dia", int(enabled), "--metrics", b], Path(str(prefix) + ".candidate.log"))
                results.append({"baseline": json.loads(a.read_text()), "candidate": json.loads(b.read_text()), "runs": [baseline_run, candidate_run]})
            medians_a = [entry["baseline"]["cpu_submission_wait_ms"]["p50"] for entry in results]
            medians_b = [entry["candidate"]["cpu_submission_wait_ms"]["p50"] for entry in results]
            overhead = float(np.median(np.subtract(medians_b, medians_a)))
            status = "PASS" if overhead <= 0.1 else "FAIL"
            if enabled:
                status = "FAIL" if min(medians_b) > 16.67 else "BLOCKED"
                if any(entry["candidate"]["lighting_scene_uploads"] != 1 for entry in results):
                    status = "FAIL"
            report["pairs"]["enabled" if enabled else "disabled"] = {"status": status, "cpu_median_overhead_ms": overhead, "pairs": results}
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        if fingerprint(frozen) != hashes:
            raise RuntimeError("Frozen candidate changed")
        if baseline_fingerprint() != baseline_hashes:
            raise RuntimeError("Baseline inputs changed")
    except Exception as error:
        report["failure"] = str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output, flush=True)


if __name__ == "__main__":
    main()
