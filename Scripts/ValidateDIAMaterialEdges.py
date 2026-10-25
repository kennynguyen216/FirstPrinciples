import argparse
import json
import time

import numpy as np
from PIL import Image

from GenerateDIAFixtures import entity, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, read_pfm, run
from ValidateDIADynamics import read_rgba


def edge_check(raw, image):
    mask = np.abs(raw[..., 3]) > 0
    error = float(np.max(np.abs(raw[..., :3][mask] - image[mask]))) if mask.any() else None
    variation = float(np.ptp(raw[..., 0][mask])) if mask.any() else 0
    protected = bool(np.all(raw[..., 3][mask] < 0))
    return {
        "status": "PASS" if mask.all() and protected and variation > 0.02 and error is not None and error <= 1e-5 else "FAIL",
        "maximum_linear_error": error, "raw_range": variation,
        "receiver_pixels": int(np.count_nonzero(mask)), "filter_exclusion_flag": protected,
    }


def self_check():
    raw = np.zeros((8, 8, 4), dtype=np.float32)
    raw[:, ::2, :3] = 1
    raw[..., 3] = -1
    image = raw[..., :3].copy()
    assert edge_check(raw, image)["status"] == "PASS"
    blurred = (image + np.roll(image, 1, axis=1) + np.roll(image, -1, axis=1)) / 3
    assert edge_check(raw, blurred)["status"] == "FAIL"
    raw[..., 3] = 1
    assert edge_check(raw, image)["status"] == "FAIL"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("Material edge metric self-check PASS")
        return

    output = ROOT / "Intermediate/DIAEvidence" / (time.strftime("%Y%m%d-%H%M%S") + "-material-edges")
    output.mkdir(parents=True)
    frozen, _ = freeze(output)
    textures = frozen / "Content/Textures/MaterialEdges"
    textures.mkdir(parents=True)
    camera = entity("Material edge camera", {
        "Name": {"Name": '"Material edge camera"'},
        "Transform": {"Position": "0 0 4", "Rotation": "0 0 0"},
        "Camera": {"Perspective": "true", "FieldOfView": "90", "NearClip": "0.1", "FarClip": "128"},
        "DIA": {
            "Enabled": "true", "SkyColor": "1 1 1 1", "SkyIntensity": "1",
            "TraceDistance": "128", "DiffuseSamples": "1", "ReflectionSamples": "1",
            "CacheUpdates": "4096", "InternalHeight": "64",
        },
    })
    y, x = np.indices((8, 8))
    cases = {
        "BaseColor": ((x + y) % 2, "BaseColorMap", "Specular 0\n"),
        "AO": (x % 2, "AmbientOcclusionMap", "AmbientOcclusionStrength 1\nSpecular 0\n"),
        "Metallic": (y % 2, "MetallicMap", "Metallic 1\nSpecular 0\n"),
        "Specular": ((x + y) % 2, "SpecularMap", "Specular 1\n"),
    }
    for name, (pattern, field, settings) in cases.items():
        pixels = np.repeat((pattern * 255).astype(np.uint8)[..., None], 3, axis=-1)
        Image.fromarray(pixels).save(textures / f"{name}.png")
        material = f"MaterialEdges{name}"
        (frozen / f"Content/Materials/DIA/{material}.material").write_text(
            'Shader "Lit"\nBaseColor 1 1 1 1\nRoughness 0.7\n' + settings +
            f'{field} "Textures/MaterialEdges/{name}.png"\n'
        )
        (frozen / f"Content/Scenes/DIA/{material}.scene").write_text(
            "version 2\n\n" + camera + "\n\n" +
            mesh("Material edge receiver", "0 0 0", "8 8 1", material, shape="Quad") + "\n"
        )
    hashes = fingerprint(frozen)
    report = {
        "source_hashes": hashes, "environment": environment(), "checks": {}, "runs": {},
        "reference": "A single diffuse ray always sees unit sky. The raw diffuse contribution is deterministic, so filtering must preserve each material texel.",
        "settings": {
            "extent": [64, 64], "texture_extent": [8, 8], "frames": 66, "maximum_linear_error": 1e-5,
            "camera_steps_metres": [0.03125, 0.001953125], "camera_steps_pixels_per_axis": [0.25, 0.015625],
        },
    }
    print("Evidence:", output, flush=True)
    try:
        for name in cases:
            folder = output / name
            folder.mkdir()
            sequence = folder / "events.txt"
            sequence.write_text(
                'at 0 capture first\nat 63 capture settled\n'
                'at 64 set "Material edge camera" Transform Position 0.03125 0.03125 4\nat 64 capture quarter_pixel\n'
                'at 65 set "Material edge camera" Transform Position 0.033203125 0.033203125 4\nat 65 capture small_step\n'
            )
            report["runs"][name] = run(frozen / "Binaries/Release/Runtime.exe", [
                f"Scenes/DIA/MaterialEdges{name}.scene", "--width", 64, "--height", 64,
                "--frames", 66, "--warmup", 0, "--seed", 911, "--viewmode", "diffuse",
                "--sequence", sequence, "--capture-dir", folder,
            ], folder / "runtime.log")
            for stage in ("first", "settled", "quarter_pixel", "small_step"):
                raw = read_rgba(folder / f"{stage}.raw0.rgba")
                image = read_pfm(folder / f"{stage}.pfm")
                report["checks"][f"{name}_{stage}"] = edge_check(raw, image)
        assert fingerprint(frozen) == hashes
        report["status"] = "PASS" if all(check["status"] == "PASS" for check in report["checks"].values()) else "FAIL"
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
