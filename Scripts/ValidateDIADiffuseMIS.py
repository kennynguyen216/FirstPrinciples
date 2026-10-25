import argparse
import json

import numpy as np


TRIALS = 131072


def power(a, b):
    return a * a / np.maximum(a * a + b * b, 1e-300)


def distribution(emission):
    total = np.sum(emission)
    return np.full(emission.size, 1 / emission.size) if total == 0 else 0.05 / emission.size + 0.95 * emission / total


def quadrature(height, half, emission, order, rho=0.5, front=True, two_sided=False, unlit=False, offset=0.0):
    if unlit or not (front or two_sided):
        return 0.0
    nodes, weights = np.polynomial.legendre.leggauss(order)
    value = 0.0
    side = int(np.sqrt(emission.size))
    assert side * side == emission.size
    width = 2 * half / side
    for cell in range(emission.size):
        x = (nodes + 1) * width / 2 + cell % side * width - half + offset
        y = (nodes + 1) * width / 2 + cell // side * width - half
        radius2 = x[:, None] ** 2 + y[None, :] ** 2 + height * height
        kernel = height * height / (np.pi * radius2 * radius2)
        value += emission[cell] * np.sum(kernel * weights[:, None] * weights[None, :]) * width * width / 4
    return float(rho * value)


def estimate(height, half, emission, rho=0.5, front=True, two_sided=False, unlit=False,
             limit=np.inf, area_samples=4, cosine_samples=1, offset=0.0, diffuse_distance=np.inf):
    if unlit or rho == 0 or not (front or two_sided):
        return np.zeros(TRIALS), np.zeros(TRIALS)
    probability = distribution(emission)
    side = int(np.sqrt(emission.size))
    assert side * side == emission.size
    width = 2 * half / side

    def area_estimate(count, rng):
        select = (np.arange(count)[None, :] + rng.random((TRIALS, count))) / count
        cell = np.searchsorted(np.cumsum(probability), select)
        x = (cell % side + rng.random(cell.shape)) * width - half + offset
        y = (cell // side + rng.random(cell.shape)) * width - half
        distance = np.sqrt(x * x + y * y + height * height)
        cosine = height / distance
        light_pdf = probability[cell] / (width * width) * distance * distance / cosine
        cosine_pdf = cosine / np.pi
        contribution = rho * emission[cell] * cosine_pdf / (count * light_pdf)
        return contribution, light_pdf, cosine_pdf, distance

    baseline = area_estimate(4, np.random.default_rng(911))[0].sum(axis=1)
    rng = np.random.default_rng(911)
    contribution, light_pdf, cosine_pdf, distance = area_estimate(area_samples, rng)
    competing_density = np.where(distance < diffuse_distance, cosine_samples * cosine_pdf, 0)
    area = contribution * power(area_samples * light_pdf, competing_density)
    radius = np.sqrt(rng.random((TRIALS, cosine_samples)))
    angle = 2 * np.pi * rng.random(radius.shape)
    dz = np.sqrt(1 - radius * radius)
    travel = height / dz
    hit_x = radius * np.cos(angle) * travel
    hit_y = radius * np.sin(angle) * travel
    visible = (np.abs(hit_x - offset) < half) & (np.abs(hit_y) < half) & (travel < limit)
    ix = np.clip(np.floor((hit_x - offset + half) / width).astype(int), 0, side - 1)
    iy = np.clip(np.floor((hit_y + half) / width).astype(int), 0, side - 1)
    hit_cell = ix + side * iy
    hit_light_pdf = probability[hit_cell] / (width * width) * travel * travel / dz
    cosine = rho * emission[hit_cell] / cosine_samples * power(cosine_samples * dz / np.pi, area_samples * hit_light_pdf)
    return baseline, area.sum(axis=1) + np.where(visible, cosine, 0).sum(axis=1)


def accuracy(values, reference, integration_error):
    error = abs(float(values.mean()) - reference)
    standard_error = float(values.std(ddof=1) / np.sqrt(values.size))
    allowed = 6 * standard_error + integration_error + max(reference * 1e-7, 1e-12)
    return {"status": "PASS" if error <= allowed else "FAIL", "mean": float(values.mean()),
            "reference": reference, "absolute_error": error, "standard_error": standard_error,
            "allowed_error": allowed}


def self_check():
    p = np.geomspace(1e-10, 1e10, 100)
    assert np.allclose(power(p, 1) + power(1, p), 1)
    for area_count, cosine_count in ((1, 1), (4, 1), (4, 4)):
        assert np.max(power(area_count * p, cosine_count) / (area_count * p)) <= 0.5 / cosine_count
        crossing = cosine_count / area_count
        assert np.isclose(power(area_count * crossing, cosine_count) / (area_count * crossing), 0.5 / cosine_count)
        for distance in (64.0, 128.0, 256.0):
            competing_density = cosine_count * 0.35 if distance < 128 else 0.0
            area_weight = power(area_count * 0.2, competing_density)
            cosine_weight = power(cosine_count * 0.35, area_count * 0.2) if distance < 128 else 0.0
            assert np.isclose(area_weight + cosine_weight, 1.0)
        assert power(area_count * 0.2, cosine_count * 0.35) < 1.0
    assert np.isclose(distribution(np.array([12, 0, 3, 8])).sum(), 1)
    assert np.all(distribution(np.array([12, 0, 3, 8])) > 0)
    assert np.isclose(distribution(np.zeros(16)).sum(), 1)
    assert np.isclose(quadrature(0.1, 0.6, np.full(4, 12.0), 64),
                      quadrature(0.1, 0.6, np.full(16, 12.0), 64))
    assert accuracy(np.ones(100), 1, 0)["status"] == "PASS"
    assert accuracy(np.ones(100), 2, 0)["status"] == "FAIL"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--samples", type=int, choices=[1, 4])
    parser.add_argument("--area-samples", type=int, choices=[1, 4, 16])
    parser.add_argument("--cosine-samples", type=int, choices=[1, 4])
    args = parser.parse_args()
    if args.samples is not None and (args.area_samples is not None or args.cosine_samples is not None):
        parser.error("Use --samples alone or separate area and cosine counts")
    area_samples = args.samples if args.samples is not None else args.area_samples or 4
    cosine_samples = args.samples if args.samples is not None else args.cosine_samples or 1
    self_check()
    if args.self_check:
        print("PASS diffuse MIS self-check")
        return
    report = {"scope": "Independent CPU direct-light integration for a Lambertian point and a rectangular emitter with constant rectangular emission cells. Area sampling stratifies the aggregate mixture CDF. This does not reproduce engine triangle sampling or bilinear texture filtering. The baseline always uses four area samples. No renderer execution, cache, punctual light or indirect transport.",
              "trials": TRIALS, "seed": 911, "baseline_area_samples": 4,
              "candidate_area_samples": area_samples, "candidate_cosine_samples": cosine_samples, "checks": {}}
    cases = {"near": dict(height=0.1, half=0.6, emission=np.full(4, 12.0)),
             "far": dict(height=250.0, half=300.0, emission=np.full(4, 12.0)),
             "near_edge": dict(height=0.1, half=0.6, offset=0.605, emission=np.full(4, 12.0)),
             "far_edge": dict(height=250.0, half=300.0, offset=301.0, emission=np.full(4, 12.0)),
             "textured": dict(height=0.1, half=0.6, emission=np.array([12.0, 0, 3, 8])),
             "textured_patch": dict(height=0.1, half=0.6, offset=0.605,
                                    emission=np.array([0, 0, 0, 0, 0, 12, 12, 0, 0, 12, 12, 0, 0, 0, 0, 0])),
             "two_sided": dict(height=0.1, half=0.6, emission=np.full(4, 12.0), front=False, two_sided=True),
             "backface": dict(height=0.1, half=0.6, emission=np.full(4, 12.0), front=False),
             "zero_rho": dict(height=0.1, half=0.6, emission=np.full(4, 12.0), rho=0.0),
             "unlit": dict(height=0.1, half=0.6, emission=np.full(4, 12.0), unlit=True)}
    for name, settings in cases.items():
        reference = quadrature(**settings, order=256)
        integration_error = abs(reference - quadrature(**settings, order=128))
        assert integration_error <= max(reference * 1e-8, 1e-12)
        before, after = estimate(**settings, area_samples=area_samples, cosine_samples=cosine_samples)
        result = accuracy(after, reference, integration_error)
        bound = (1 + area_samples / (2 * cosine_samples)) * settings.get("rho", 0.5) * float(np.max(settings["emission"]))
        ratio = float(after.var() / before.var()) if before.var() > 0 else None
        result.update(maximum=float(after.max()), estimator_bound=bound, variance_ratio=ratio)
        result["bound_pass"] = bool(np.all((after >= 0) & (after <= bound + 1e-12)))
        variance_limit = 0.25 if name in ("near", "near_edge", "two_sided") else 1.02
        result["variance_limit"] = variance_limit
        result["variance_pass"] = ratio is None or ratio <= variance_limit
        if not result["bound_pass"] or not result["variance_pass"]:
            result["status"] = "FAIL"
        if reference == 0:
            assert np.array_equal(after, np.zeros(TRIALS))
        report["checks"][name] = result
    _, truncated = estimate(**cases["far"], limit=128, area_samples=area_samples, cosine_samples=cosine_samples)
    reference = report["checks"]["far"]["reference"]
    assert accuracy(truncated, reference, 0)["status"] == "FAIL"
    report["checks"]["support_negative_control"] = {"status": "PASS", "truncated_mean": float(truncated.mean()),
                                                       "reference": reference}
    for name, cutoff in (("near", 0.5), ("far", 128.0)):
        before, corrected = estimate(**cases[name], limit=cutoff, diffuse_distance=cutoff,
                                     area_samples=area_samples, cosine_samples=cosine_samples)
        result = accuracy(corrected, report["checks"][name]["reference"], 1e-12)
        result["cosine_distance"] = cutoff
        if name == "far" and area_samples == 4:
            result["matches_area_only"] = bool(np.array_equal(before, corrected))
            assert result["matches_area_only"]
        report["checks"]["finite_support_" + name] = result
    report["status"] = "PASS" if all(value["status"] == "PASS" for value in report["checks"].values()) else "FAIL"
    print(json.dumps(report, indent=2))
    if report["status"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
