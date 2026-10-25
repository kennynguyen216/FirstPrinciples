from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "Projects/Sample" if (ROOT / "Projects/Sample/Sample.fpproject").is_file() else ROOT
PROJECT_CONTENT = PROJECT / "Content"
ENGINE_CONTENT = ROOT / "Engine/Content" if (ROOT / "Engine/Content").is_dir() else ROOT / "Content/Engine"


def read_pfm(path):
    with Path(path).open("rb") as source:
        if source.readline().strip() != b"PF":
            raise ValueError(f"Not an RGB PFM: {path}")
        width, height = map(int, source.readline().split())
        scale = float(source.readline())
        data = np.frombuffer(source.read(), dtype="<f4" if scale < 0 else ">f4")
    if data.size != width * height * 3 or not np.isfinite(data).all():
        raise ValueError(f"Invalid or nonfinite PFM: {path}")
    return np.flipud(data.reshape(height, width, 3)).copy()


def region(image, bounds):
    height, width = image.shape[:2]
    x0, y0, x1, y1 = bounds
    return image[int(y0 * height):int(y1 * height), int(x0 * width):int(x1 * width)]


def luminance(image):
    return image @ np.array([0.2126, 0.7152, 0.0722])


def compare(candidate, references, bounds):
    if not references or any(reference.shape != candidate.shape for reference in references):
        raise ValueError("Candidate and reference extents must match")
    reference = np.mean(references, axis=0)
    a, b = region(candidate, bounds), region(reference, bounds)
    mean = float(luminance(b).mean())
    floor = 1e-5
    ratio = float(luminance(a).mean()) / max(mean, floor)
    error = float(np.sqrt(np.mean((a - b) ** 2))) / max(mean, floor)
    seed_means = [float(luminance(region(image, bounds)).mean()) for image in references]
    uncertainty = float(np.std(seed_means, ddof=1) / np.sqrt(len(seed_means))) / max(mean, floor) if len(seed_means) > 1 else None
    image_uncertainty = float(np.sqrt(np.mean(np.var([region(image, bounds) for image in references], axis=0, ddof=1) / len(references)))) / max(mean, floor) if len(references) > 1 else None
    status = "PASS" if 0.9 <= ratio <= 1.1 and error <= 0.2 else "FAIL"
    if mean < floor:
        status = "PASS" if float(np.max(np.abs(a))) <= floor else "FAIL"
    numerical_status = status
    if uncertainty is None or uncertainty > 0.03 or image_uncertainty > 0.1:
        status = "BLOCKED"
    return {"status": status, "numerical_status": numerical_status, "candidate_mean": float(luminance(a).mean()), "reference_mean": mean,
            "energy_ratio": ratio, "relative_rmse": error, "reference_mean_standard_error_fraction": uncertainty,
            "reference_image_standard_error_rmse_fraction": image_uncertainty}


def masked_compare(candidate, references, mask):
    if not mask.any():
        return {"status": "BLOCKED", "reason": "Empty geometry mask"}
    return compare(candidate[mask][None], [image[mask][None] for image in references], [0, 0, 1, 1])


def front_mask(shape, center, half):
    height, width = shape[:2]
    y, x = np.mgrid[:height, :width]
    tangent = np.tan(np.deg2rad(55 / 2))
    dx = (2 * (x + 0.5) / width - 1) * tangent * width / height
    dy = (1 - 2 * (y + 0.5) / height) * tangent
    distance = 5 - (center[2] + half[2])
    return (np.abs(dx * distance - center[0]) <= half[0]) & (np.abs(dy * distance - center[1]) <= half[1])


def mirror_check(candidate, references):
    height, width = candidate.shape[:2]
    y, x = np.mgrid[:height, :width]
    tangent = np.tan(np.deg2rad(55 / 2))
    dx = (2 * (x + 0.5) / width - 1) * tangent * width / height
    dy = (1 - 2 * (y + 0.5) / height) * tangent
    distance = (5 + 2.89) + (5.45 + 2.89)
    mask = (np.abs(dx * distance) <= 0.5) & (np.abs(dy * distance - 1) <= 0.5)
    receiver = front_mask(candidate.shape, [0, 0, -2.9], [2, 1.5, 0.01])
    observed = (luminance(candidate) > 3) & receiver
    reference_observed = (luminance(np.mean(references, axis=0)) > 3) & receiver
    union = np.count_nonzero(mask | observed)
    iou = np.count_nonzero(mask & observed) / max(union, 1)
    reference_iou = np.count_nonzero(mask & reference_observed) / max(np.count_nonzero(mask | reference_observed), 1)
    energy = masked_compare(candidate, references, mask)
    return {"status": "PASS" if iou >= 0.95 and reference_iou >= 0.95 and energy["status"] == "PASS" else "FAIL",
            "mask_iou": iou, "reference_mask_iou": reference_iou, "emission_energy": energy}


def sweep_check(candidate, references):
    result = {}
    for metallic in (0, 1):
        for index, roughness in enumerate((0.01, 0.2, 0.5, 0.9)):
            mask = front_mask(candidate.shape, [(index - 1.5) * 1.1, metallic - 0.5, -0.5], [0.4, 0.4, 0.4])
            result[f"metallic_{metallic}_roughness_{roughness}"] = masked_compare(candidate, references, mask)
    return result


def fixture_check(name, candidate, references, bounds):
    result = compare(candidate, references, bounds)
    if name == "Mirror": result["smooth_reflection"] = mirror_check(candidate, references)
    if name == "Sweep":
        result["material_bins"] = sweep_check(candidate, references)
        statuses = {check["status"] for check in result["material_bins"].values()} | {result["status"]}
        if "FAIL" in statuses: result["status"] = "FAIL"
        elif "BLOCKED" in statuses: result["status"] = "BLOCKED"
    return result


def input_files(base=ROOT):
    base = Path(base)
    project = base / "Projects/Sample" if (base / "Projects/Sample/Sample.fpproject").is_file() else base
    files = {"Binaries/Release/Runtime.exe": project / "Binaries/Release/Runtime.exe",
             "Binaries/Release/SDL3.dll": project / "Binaries/Release/SDL3.dll",
             "docs/DIAFixtures.json": base / "docs/DIAFixtures.json"}
    for name in ("GameScripts.dll", "GameScripts.dylib", "GameScripts.so"):
        path = project / "Binaries/Release" / name
        if path.is_file(): files["Binaries/Release/" + name] = path
    for path in project.glob("*.fpproject"):
        files[path.name] = path
    folders = [(base / name, name) for name in ("src", "Scripts", "Engine/Content")]
    folders += [(project / name, name) for name in (("Game", "Source", "Content") if project == base else ("Source", "Content"))]
    for folder, prefix in folders:
        for path in folder.rglob("*"):
            if path.is_file() and "__pycache__" not in path.parts:
                files[prefix + "/" + path.relative_to(folder).as_posix()] = path
    return dict(sorted(files.items()))


def fingerprint(base=ROOT):
    return {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in input_files(base).items()}


def freeze(output, base=ROOT):
    files = input_files(base)
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
    destination = output / "frozen"
    for name in hashes:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(files[name], target)
    if fingerprint(destination) != hashes or fingerprint(base) != hashes:
        raise RuntimeError("Build inputs changed while freezing the run")
    return destination, hashes


def environment():
    result = {"platform": os.name, "gpu": None, "ac_power": None}
    if os.name == "nt":
        class PowerStatus(ctypes.Structure):
            _fields_ = [("ACLineStatus", ctypes.c_byte), ("BatteryFlag", ctypes.c_byte),
                        ("BatteryLifePercent", ctypes.c_byte), ("SystemStatusFlag", ctypes.c_byte),
                        ("BatteryLifeTime", ctypes.c_uint32), ("BatteryFullLifeTime", ctypes.c_uint32)]
        power = PowerStatus()
        if ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(power)):
            result["ac_power"] = int(power.ACLineStatus)
        command = ["powershell", "-NoProfile", "-Command",
                   "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion | ConvertTo-Json -Compress"]
        result["gpu"] = json.loads(subprocess.check_output(command, text=True))
    return result


def run(runtime, arguments, log_path, timeout=120):
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    command = [str(runtime), *map(str, arguments)]
    start = time.monotonic()
    with log_path.open("w", encoding="utf-8") as log:
        completed = subprocess.run(command, cwd=Path(runtime).resolve().parents[2], stdout=log, stderr=subprocess.STDOUT,
                                   timeout=timeout, startupinfo=startup)
    text = log_path.read_text(encoding="utf-8")
    if completed.returncode or any(marker in text for marker in ("[error]", "Validation Error", "device lost")):
        raise RuntimeError(f"Runtime failed; inspect {log_path}")
    return {"command": command, "seconds": time.monotonic() - start, "exit_code": completed.returncode}


def self_check():
    temporary_root = ROOT / "Intermediate"
    temporary_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="dia-layout-", dir=temporary_root) as temporary:
        temporary = Path(temporary).resolve()
        assert temporary.is_relative_to(temporary_root.resolve())
        source = temporary / "source"
        for name in ("Projects/Sample/Binaries/Release/Runtime.exe", "Projects/Sample/Binaries/Release/SDL3.dll",
                     "Projects/Sample/Binaries/Release/GameScripts.dll", "Projects/Sample/Sample.fpproject",
                     "Projects/Sample/Source/Game.cpp", "Projects/Sample/Content/Scenes/DIA/Review.scene",
                     "Engine/Content/Shaders/DIATrace.frag.spv", "docs/DIAFixtures.json", "src/example.cpp"):
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
        files = input_files(source)
        assert "Sample.fpproject" in files and "Source/Game.cpp" in files and "Binaries/Release/GameScripts.dll" in files
        assert files["Content/Scenes/DIA/Review.scene"] == source / "Projects/Sample/Content/Scenes/DIA/Review.scene"
        frozen, hashes = freeze(temporary / "evidence", source)
        assert fingerprint(frozen) == hashes == fingerprint(source)
        (frozen / "Engine/Content/Shaders/DIATrace.frag.spv").write_text("changed")
        assert fingerprint(frozen) != hashes
    image = np.ones((8, 8, 3), dtype=np.float32)
    good = compare(image, [image, image, image], [0, 0, 1, 1])
    assert good["status"] == "PASS" and good["energy_ratio"] == 1
    bad = compare(image * 1.2, [image, image, image], [0, 0, 1, 1])
    assert bad["status"] == "FAIL"
    assert compare(image, [image], [0, 0, 1, 1])["status"] == "BLOCKED"
    assert compare(image * 0, [image * 0] * 3, [0, 0, 1, 1])["status"] == "PASS"
    sweep = np.ones((64, 64, 3), dtype=np.float32)
    references = [sweep.copy() for _ in range(3)]
    mask = front_mask(sweep.shape, [-1.65, -0.5, -0.5], [0.4, 0.4, 0.4])
    references[0][mask] *= 1.5
    references[1][mask] *= 0.5
    bounds = [0.1, 0.55, 0.9, 0.9]
    assert compare(sweep, references, bounds)["status"] == "PASS"
    assert fixture_check("Sweep", sweep, references, bounds)["status"] == "BLOCKED"
    assert fixture_check("Sweep", sweep, [sweep] * 3, bounds)["status"] == "PASS"
    assert fixture_check("Sweep", sweep * 1.2, [sweep] * 3, bounds)["status"] == "FAIL"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", action="append")
    parser.add_argument("--samples", type=int, default=1024)
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print("DIA metric self-checks passed")
        return
    if not 1 <= args.samples <= 16384:
        parser.error("samples must be in 1..16384")
    manifest = json.loads((ROOT / "docs/DIAFixtures.json").read_text())
    fixtures = args.fixture or list(manifest["fixtures"])
    if any(name not in manifest["fixtures"] for name in fixtures):
        parser.error("unknown fixture")
    output = ROOT / "Intermediate/DIAEvidence" / time.strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True)
    frozen, immutable = freeze(output)
    report = {"mode": "diagnostic" if args.diagnostic else "quality", "source_hashes": immutable,
              "environment": environment(), "fixtures": {}, "independent_review": "PENDING",
              "gpu_timing": "BLOCKED: SDL_GPU does not expose GPU critical-time queries"}
    (output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    runtime = frozen / "Binaries/Release/Runtime.exe"
    try:
        for name in fixtures:
            fixture = manifest["fixtures"][name]
            seeds = manifest["reference_seeds"][:1] if args.diagnostic else manifest["reference_seeds"]
            reference_images = []
            candidate_images = []
            runs = []
            for seed in seeds:
                print(f"{name}, seed {seed}: 64x64, {args.samples} reference samples, 360/600 frames", flush=True)
                prefix = output / f"{name}-{seed}"
                candidate_path = prefix.with_suffix(".candidate.pfm")
                reference_path = prefix.with_suffix(".reference.pfm")
                arguments = [fixture["scene"], "--frames", 360 if args.diagnostic else 600, "--warmup", 300,
                             "--width", 64, "--height", 64, "--internal-height", 64, "--seed", seed,
                             "--hdr", candidate_path, "--reference", reference_path, "--reference-size", 64,
                             "--reference-spp", args.samples, "--metrics", prefix.with_suffix(".metrics.json")]
                runs.append(run(runtime, arguments, prefix.with_suffix(".log")))
                reference_images.append(read_pfm(reference_path))
                candidate_images.append(read_pfm(candidate_path))
                if fingerprint(frozen) != immutable:
                    raise RuntimeError("The build or input fixtures changed during the run")
            result = fixture_check(name, np.mean(candidate_images, axis=0), reference_images, fixture["receiver_region"])
            result["runs"] = runs
            result["status"] = "DIAGNOSTIC" if args.diagnostic else result["status"]
            report["fixtures"][name] = result
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            print(name, result["status"], "ratio", result["energy_ratio"], "RMSE", result["relative_rmse"], flush=True)
        if "Sealed" in report["fixtures"] and "Open" in report["fixtures"]:
            sealed = report["fixtures"]["Sealed"]["candidate_mean"]
            opened = report["fixtures"]["Open"]["candidate_mean"]
            report["sealed_room_leakage"] = {"ratio": sealed / max(opened, 1e-5),
                                             "status": "PASS" if sealed <= opened * 0.01 else "FAIL"}
    except Exception as error:
        report["failure"] = str(error)
        raise
    finally:
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output, flush=True)


if __name__ == "__main__":
    main()
