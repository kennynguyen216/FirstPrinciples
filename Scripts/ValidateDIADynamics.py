import json
import time
from pathlib import Path

import numpy as np

from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, region, run


def read_rgba(path):
    with Path(path).open("rb") as source:
        if source.readline().strip() != b"FPRGBA32F":
            raise ValueError(f"Invalid diagnostic: {path}")
        width, height = map(int, source.readline().split())
        pixels = np.frombuffer(source.read(), dtype="<f4")
    if pixels.size != width * height * 4 or not np.isfinite(pixels).all():
        raise ValueError(f"Invalid diagnostic pixels: {path}")
    return pixels.reshape(height, width, 4)


def geometry_check(prefix):
    expected = read_rgba(str(prefix) + ".geometry.rgba")
    actual = read_rgba(str(prefix) + ".raw2.rgba")
    identity = np.abs(read_rgba(str(prefix) + ".raw0.rgba")[..., 3])
    hits = expected[..., 3] > 0
    mismatched = int(np.count_nonzero(identity != expected[..., 3]))
    error = float(np.max(np.linalg.norm(actual[..., :3][hits] - expected[..., :3][hits], axis=-1))) if hits.any() else 0
    reflected = read_rgba(str(prefix) + ".reflected.rgba")
    specular_id = np.abs(read_rgba(str(prefix) + ".raw1.rgba")[..., 3])
    eligible = reflected[..., 3] >= 0
    reflected_mismatched = int(np.count_nonzero(specular_id[eligible] != reflected[..., 3][eligible]))
    diagnostics = read_rgba(str(prefix) + ".raw4.rgba")[..., 3].astype(np.uint32)
    cache_incomplete = int(np.count_nonzero(read_rgba(str(prefix) + ".cache.rgba")[..., 3] < 0))
    return {"status": "PASS" if mismatched == 0 and error <= 0.001 and reflected_mismatched == 0 and not diagnostics.any() and cache_incomplete == 0 else "FAIL",
            "primary_identity_mismatches": mismatched, "primary_position_max_error_metres": error,
            "smooth_reflection_identity_mismatches": reflected_mismatched,
            "unknown_query_pixels": int(np.count_nonzero(diagnostics & 1)), "exhausted_query_pixels": int(np.count_nonzero(diagnostics & 2)),
            "invalid_query_pixels": int(np.count_nonzero(diagnostics & 4)), "cache_incomplete_samples": cache_incomplete}


def response(before, after, steady):
    bounds = [0.1, 0.55, 0.9, 0.9]
    means = [float(luminance(region(image, bounds)).mean()) for image in (before, after, steady)]
    target_error = abs(means[1] - means[2]) / max(means[2], 1e-5)
    ghost = means[1] / max(means[0], 1e-5) if means[2] < 1e-5 else None
    passed = ghost <= 0.01 if ghost is not None else target_error <= 0.1
    return {"status": "PASS" if passed else "FAIL", "means": means,
            "target_error_fraction": target_error, "ghost_fraction": ghost, "frames": 30}


def main():
    image = np.ones((8, 8, 3), dtype=np.float32)
    assert response(image, image, image)["status"] == "PASS"
    assert response(image, image * 0, image * 0)["ghost_fraction"] == 0
    assert response(image, image * 0.1, image * 0)["status"] == "FAIL"
    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-dynamic")
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    runtime = frozen / "Binaries/Release/Runtime.exe"
    report = {"source_hashes": hashes, "environment": environment(), "runs": {}, "captures": {},
              "responses": {}, "independent_review": "PENDING"}
    scenarios = {
        "Point": [(299, 'capture before'), (300, 'set "PointLight" PointLight Intensity 0'),
                  (300, 'capture off0'), (330, 'capture off30'), (599, 'capture offsteady'),
                  (600, 'set "PointLight" PointLight Intensity 8'), (630, 'capture on30'),
                  (899, 'capture onsteady'), (900, 'set "Receiver" Transform Position 1.2 -0.5 -0.5'),
                  (900, 'capture moved0'), (930, 'capture moved30'), (1199, 'capture movedsteady'),
                  (1200, 'material "Receiver" BaseColor 0.1 0.1 0.1 1'), (1230, 'capture material30'),
                  (1499, 'capture materialsteady'), (1500, 'delete "Receiver"'),
                  (1500, 'capture deleted0'), (1530, 'capture deleted30'), (1799, 'capture deletedsteady'),
                  (1800, 'cut'), (1800, 'capture cut0'), (1830, 'capture cut30'),
                  (1900, 'resize 96 64'), (1930, 'capture resized30'), (1999, 'capture resizedsteady'),
                  (2000, 'set "Main Camera" DIA Enabled false'),
                  (2010, 'set "Main Camera" DIA Enabled true'), (2040, 'capture enabled30'),
                  (2099, 'capture enabledsteady')],
        "Mirror": [(299, 'capture before'), (300, 'set "Offscreen emitter" Transform Position 1 1 5.5'),
                   (300, 'capture moved0'), (330, 'capture moved30'), (599, 'capture movedsteady'),
                   (600, 'delete "Offscreen emitter"'), (600, 'capture deleted0'),
                   (630, 'capture deleted30'), (899, 'capture deletedsteady')],
        "Emissive": [(299, 'capture before'), (300, 'material "Emitter panel" EmissiveIntensity 0'),
                     (300, 'capture off0'), (330, 'capture off30'), (599, 'capture offsteady'),
                     (600, 'material "Emitter panel" EmissiveIntensity 12'),
                     (630, 'capture on30'), (899, 'capture onsteady')],
        "Mixed": [(299, 'capture before'), (300, 'set "Mixed surfaces" Transform Position 1 0 0'),
                  (300, 'capture moved0'), (330, 'capture moved30'), (599, 'capture movedsteady'),
                  (600, 'material "Mixed surfaces" EmissiveIntensity 2'), (630, 'capture material30'),
                  (899, 'capture materialsteady'), (900, 'set "Mixed surfaces" Mesh Mesh "Sphere"'),
                  (930, 'capture topology30')],
        "Travel": [(299, 'capture room0'), (300, 'set "Main Camera" Transform Position 12 0 5'),
                   (300, 'cut'), (300, 'capture room1_0'), (330, 'capture room1_30'),
                   (599, 'capture room1_steady'), (600, 'set "Main Camera" Transform Position 24 0 5'),
                   (600, 'cut'), (630, 'capture room2_30'), (899, 'capture room2_steady'),
                   (900, 'set "Main Camera" Transform Position 36 0 5'), (900, 'cut'),
                   (930, 'capture room3_30'), (1199, 'capture room3_steady'),
                   (1200, 'view 2'), (1200, 'set "Main Camera" Transform Position 0 0 5'),
                   (1230, 'capture second_view'), (1300, 'view 1'),
                   (1300, 'set "Main Camera" Transform Position 36 0 5'), (1330, 'capture first_view'),
                   (1360, 'view 3'), (1390, 'capture evicted_view'),
                   (1400, 'load "Scenes/DIA/Point.scene"'), (1430, 'capture unloaded'),
                   (1500, 'load "Scenes/DIA/Travel.scene"'), (1530, 'capture reloaded1'),
                   (1600, 'load "Scenes/DIA/Point.scene"'),
                   (1700, 'load "Scenes/DIA/Travel.scene"'), (1730, 'capture reloaded2'),
                   (1800, 'load "Scenes/DIA/Point.scene"'),
                   (1900, 'load "Scenes/DIA/Travel.scene"'), (1930, 'capture reloaded3')],
    }
    try:
        for name, events in scenarios.items():
            folder = output / name
            folder.mkdir()
            sequence = folder / "events.txt"
            sequence.write_text("\n".join(f"at {frame} {action}" for frame, action in events) + "\n")
            print(f"{name}: motion, revisions and geometry captures", flush=True)
            report["runs"][name] = run(runtime, [f"Scenes/DIA/{name}.scene", "--frames", events[-1][0] + 1,
                "--warmup", 300, "--width", 64, "--height", 64, "--internal-height", 64,
                "--metrics", folder / "metrics.json", "--sequence", sequence, "--capture-dir", folder], folder / "runtime.log")
            for _, action in events:
                if not action.startswith("capture "):
                    continue
                label = action.split()[1]
                prefix = folder / label
                report["captures"][f"{name}/{label}"] = geometry_check(prefix)
                metadata = json.loads(prefix.with_suffix(".json").read_text())
                if metadata["resident_bytes_bound"] > 256 * 1024 * 1024 or metadata["pending_age_bound_frames"] > 30:
                    report["captures"][f"{name}/{label}"]["status"] = "FAIL"
                report["captures"][f"{name}/{label}"].update(metadata)
            for label in ("off", "on", "moved", "material", "deleted", "resized", "enabled", "room1_", "room2_", "room3_"):
                at30, steady = folder / f"{label}30.pfm", folder / f"{label}steady.pfm"
                if at30.exists() and steady.exists():
                    before = folder / "before.pfm" if (folder / "before.pfm").exists() else folder / "room0.pfm"
                    report["responses"][f"{name}/{label}"] = response(read_pfm(before), read_pfm(at30), read_pfm(steady))
                    uploads = [json.loads(path.with_suffix(".json").read_text())["scene_uploads"] for path in (at30, steady)]
                    report["responses"][f"{name}/{label}"]["steady_scene_uploads"] = uploads[1] - uploads[0]
                    if uploads[1] != uploads[0]:
                        report["responses"][f"{name}/{label}"]["status"] = "FAIL"
            if name == "Travel":
                memory = [json.loads((folder / f"reloaded{i}.json").read_text())["resident_bytes_bound"] for i in (1, 2, 3)]
                report["memory_plateau"] = {"status": "PASS" if max(memory) <= memory[0] * 1.05 else "FAIL", "bytes_bound": memory}
                views = [json.loads((folder / f"{label}.json").read_text())["resident_bytes_bound"] for label in ("first_view", "evicted_view")]
                report["view_history_budget"] = {"status": "PASS" if views[1] <= views[0] * 1.05 else "FAIL", "bytes_bound": views}
                report["responses"]["Travel/evicted_view"] = response(read_pfm(folder / "first_view.pfm"), read_pfm(folder / "evicted_view.pfm"), read_pfm(folder / "room3_steady.pfm"))
            builds = [json.loads((folder / f"{label}.json").read_text())["immutable_mesh_builds"] for label in ("before", "moved0", "deleted0") if (folder / f"{label}.json").exists()]
            report["runs"][name]["rigid_immutable_rebuilds"] = max(builds) - min(builds) if builds else 0
            if fingerprint(frozen) != hashes:
                raise RuntimeError("Frozen inputs changed")
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    except Exception as error:
        report["failure"] = str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output, flush=True)


if __name__ == "__main__":
    main()
