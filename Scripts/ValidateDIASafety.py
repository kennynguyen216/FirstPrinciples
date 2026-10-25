import json
import os
import subprocess
import time

import numpy as np

from GenerateDIAFixtures import header, light, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, region, run


def main():
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-safety")
    output.mkdir(parents=True)
    frozen, _ = freeze(output)
    scenes = frozen / "Content/Scenes/DIA"
    materials = frozen / "Content/Materials/DIA"
    (materials / "Unit.material").write_text('Shader "Lit"\nBaseColor 1 1 1 1\nSpecular 0\nRoughness 0.7\n')
    (materials / "TwoSided.material").write_text('Shader "Lit"\nBaseColor 1 1 1 1\nSpecular 0\nRoughness 0.7\nTwoSided true\n')
    for side in ("front", "back"):
        camera = header()[0]
        if side == "back":
            camera = camera.replace('Position 0 0 5', 'Position 0 0 -5').replace('Rotation 0 0 0', 'Rotation 0 180 0')
        (scenes / f"TwoSided-{side}.scene").write_text("version 2\n\n" + "\n\n".join([camera,
            mesh("Two sided receiver", "0 0 0", "8 8 1", "TwoSided", shape="Quad"),
            light(position="0 0 -2")]) + "\n")
    (scenes / "Unit.scene").write_text("version 2\n\n" + "\n\n".join(header(1) + [mesh("Unit receiver", "0 0 0", "100 100 1", "Unit", shape="Quad")]) + "\n")
    (scenes / "OverBudget.scene").write_text("version 2\n\n" + "\n\n".join(header() + [mesh(f"Object {i}", f"{i * 2} 0 0", "1 1 1") for i in range(257)]) + "\n")
    hashes = fingerprint(frozen)
    runtime = frozen / "Binaries/Release/Runtime.exe"
    report = {"source_hashes": hashes, "environment": environment(), "checks": {}, "runs": {}, "independent_review": "PENDING"}
    common = ["--width", 64, "--height", 64, "--internal-height", 64, "--frames", 64, "--warmup", 0]
    try:
        run(runtime, ["Scenes/DIA/Unit.scene", *common, "--viewmode", "diffuse", "--hdr", output / "unit.pfm", "--metrics", output / "unit.json",
            "--reference", output / "unit-reference.pfm", "--reference-size", 32, "--reference-spp", 1024, "--reference-bounces", 2], output / "unit.log")
        unit = read_pfm(output / "unit.pfm")
        error = float(np.max(np.abs(unit - 1)))
        coverage = float(np.mean(np.all(np.isfinite(unit), axis=-1) & (np.max(np.abs(unit - 1), axis=-1) <= 0.02)))
        report["checks"]["constant_input"] = {"status": "PASS" if error <= 0.02 and coverage >= 0.95 else "FAIL", "max_absolute_error": error, "valid_gather_coverage": coverage}
        reference_error = abs(float(read_pfm(output / "unit-reference.pfm").mean()) - 1)
        report["checks"]["reference_constant_input"] = {"status": "PASS" if reference_error <= 0.02 else "FAIL", "mean_absolute_error": reference_error}
        for side in ("front", "back"):
            candidate, reference = output / f"two-sided-{side}.pfm", output / f"two-sided-{side}-reference.pfm"
            run(runtime, [f"Scenes/DIA/TwoSided-{side}.scene", *common, "--viewmode", "cache", "--hdr", candidate,
                "--reference", reference, "--reference-size", 64, "--reference-spp", 1, "--reference-bounces", 1], output / f"two-sided-{side}.log")
            a, b = read_pfm(candidate), read_pfm(reference)
            error = float(np.sqrt(np.mean((a - b) ** 2))) / max(float(luminance(b).mean()), 1e-5)
            report["checks"][f"two_sided/{side}"] = {"status": "PASS" if error <= 0.01 else "FAIL", "relative_rmse": error}
        for name, extent in (("Point", 64), ("Emissive", 64), ("Emissive", 256), ("BrightPatch", 64), ("Mirror", 64)):
            case = name if extent == 64 else f"{name}-{extent}"
            print(f"{case}: source ownership closure", flush=True)
            images = {}
            skipped = {}
            for mode in ("lit", "direct", "diffuse", "reflections"):
                path = output / f"{case}-{mode}.pfm"
                run(runtime, [f"Scenes/DIA/{name}.scene", *common, "--width", extent, "--height", extent, "--viewmode", mode, "--hdr", path, "--metrics", output / f"{case}-{mode}.json"], output / f"{case}-{mode}.log")
                images[mode] = read_pfm(path)
                metrics = json.loads((output / f"{case}-{mode}.json").read_text())
                assert metrics['frames'] == metrics['lighting_frames'] == 64
                skipped[mode] = metrics['skipped_presentations']
            error = float(np.max(np.abs(images["lit"] - images["direct"] - images["diffuse"] - images["reflections"])))
            scale = max(float(np.max(images["lit"])), 1e-5)
            report["checks"][f"{case}/closure"] = {"status": "PASS" if error / scale <= 1e-5 else "FAIL", "max_error_fraction": error / scale, "skipped_presentations": skipped}
        for name in ("Point", "Spot", "BlockerBefore", "BlockerBehind", "ThinWall"):
            print(f"{name}: direct analytic agreement", flush=True)
            candidate, reference = output / f"{name}-direct.pfm", output / f"{name}-reference.pfm"
            run(runtime, [f"Scenes/DIA/{name}.scene", *common, "--viewmode", "direct", "--hdr", candidate,
                "--reference", reference, "--reference-bounces", 1, "--reference-spp", 1, "--reference-size", 64,
                "--metrics", output / f"{name}-analytic.json"], output / f"{name}-analytic.log")
            a, b = region(read_pfm(candidate), [0.1, 0.55, 0.9, 0.9]), region(read_pfm(reference), [0.1, 0.55, 0.9, 0.9])
            error = float(np.sqrt(np.mean((a - b) ** 2))) / max(float(luminance(b).mean()), 1e-5)
            report["checks"][f"{name}/direct"] = {"status": "PASS" if error <= 0.01 else "FAIL", "relative_rmse": error}
        for iteration in range(2):
            run(runtime, ["Scenes/DIA/Point.scene", *common, "--reference", output / f"determinism{iteration}.pfm",
                "--reference-size", 32, "--reference-spp", 128], output / f"determinism{iteration}.log")
        report["checks"]["reference_determinism"] = {"status": "PASS" if (output / "determinism0.pfm").read_bytes() == (output / "determinism1.pfm").read_bytes() else "FAIL"}
        for name, arguments, marker in (
            ("instance_capacity", ["Scenes/DIA/OverBudget.scene", *common], "instance budget exceeded"),
            ("view_memory", ["Scenes/DIA/Point.scene", "--width", 1920, "--height", 1080, "--internal-height", 1080, "--frames", 1], "residency budget"),
        ):
            startup = None
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = subprocess.SW_HIDE
            with (output / f"{name}.log").open("w") as log:
                result = subprocess.run([str(runtime), *map(str, arguments)], cwd=frozen, stdout=log, stderr=subprocess.STDOUT, timeout=30, startupinfo=startup)
            text = (output / f"{name}.log").read_text()
            report["checks"][name] = {"status": "PASS" if result.returncode == 1 and marker in text else "FAIL", "exit_code": result.returncode}
        if fingerprint(frozen) != hashes:
            raise RuntimeError("Frozen inputs changed")
    except Exception as error:
        report["failure"] = str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output, flush=True)


if __name__ == "__main__":
    main()
