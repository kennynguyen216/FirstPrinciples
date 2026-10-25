import argparse, json, shutil, time
from pathlib import Path
import numpy as np
from GenerateDIAFixtures import entity, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, read_pfm, run
from ValidateDIADynamics import read_rgba

def masking(cosine, roughness):
    a2 = roughness ** 4
    return 2 * cosine / (cosine + np.sqrt(a2 + (1 - a2) * cosine * cosine))

def expected_energy(angle, roughness, order=256):
    if roughness <= 0.02:
        return 1.0
    mu, weights = np.polynomial.legendre.leggauss(order)
    mu = (mu + 1) * 0.5
    weights = weights * 0.5
    phi = (np.arange(512) + 0.5) * (2 * np.pi / 512)
    extent = 32
    y, x = np.mgrid[:extent, :extent]
    dx = (2 * (x + 0.5) / extent - 1) * np.tan(np.deg2rad(0.5))
    dy = (1 - 2 * (y + 0.5) / extent) * np.tan(np.deg2rad(0.5))
    a = np.deg2rad(angle)
    nv = (np.cos(a) + dx * np.sin(a)) / np.sqrt(1 + dx * dx + dy * dy)
    views = np.linspace(nv.min(), nv.max(), 17)
    values = []
    for v in views:
        vh = np.sqrt(1 - v * v) * np.sqrt(1 - mu[:, None] ** 2) * np.cos(phi) + v * mu[:, None]
        nl = 2 * vh * mu[:, None] - v
        distribution = roughness ** 4 / (np.pi * (mu[:, None] ** 2 * (roughness ** 4 - 1) + 1) ** 2)
        integrand = np.where((vh > 0) & (nl > 0), distribution * masking(v, roughness) * masking(np.maximum(nl, 0), roughness) * vh / v, 0)
        values.append(float(np.sum(integrand * weights[:, None]) * (2 * np.pi / 512)))
    return float(np.interp(nv, views, values).mean())

def check_cache_history():
    for count in (1, 3, 7, 64, 195):
        for width in (1, 2, 7, 64, 4096):
            for start in (0, count // 2, count - 1):
                visits = np.zeros(count, dtype=int)
                slots = np.arange(count)
                cursor = start
                for age in range(201):
                    offset = (slots + count - cursor) % count
                    selected = offset < width
                    visits[selected] += 1
                    updates = 1 + (age * min(width, count) + offset[selected]) // count
                    assert np.array_equal(visits[selected], updates)
                    cursor = (cursor + width) % count

def main():
    check_cache_history()
    assert abs(expected_energy(0, 1) - (1 - np.log(2))) < 1e-05
    assert abs(expected_energy(85, 0.2) - expected_energy(85, 0.2, 512)) < 0.0002
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-sampling')
    output.mkdir(parents=True)
    frozen, _ = freeze(output)
    baseline_hashes = fingerprint(args.baseline) if args.baseline else None
    if args.baseline:
        shutil.copytree(args.baseline, frozen, dirs_exist_ok=True)
    scenes = frozen / 'Content/Scenes/DIA/Sampling'
    scenes.mkdir(parents=True)
    cases = {}
    for roughness in (0.01, 0.2, 0.7, 1.0):
        material = f'Sampling{int(roughness * 100)}'
        (frozen / f'Content/Materials/DIA/{material}.material').write_text(f'Shader "Lit"\nBaseColor 1 1 1 1\nMetallic 1\nRoughness {roughness}\n')
        expected = {angle: expected_energy(angle, roughness) for angle in (0, 75, 85)}
        for angle, samples in ((angle, samples) for angle in (0, 75, 85) for samples in (1, 8)):
            name = f'a{angle}_r{int(roughness * 100)}' + ('_s8' if samples == 8 else '')
            camera = entity('Sampling camera', {'Name': {'Name': '"Sampling camera"'}, 'Transform': {'Position': '0 0 5', 'Rotation': f'0 {angle} 0'}, 'Camera': {'Perspective': 'true', 'FieldOfView': '1', 'NearClip': '.1', 'FarClip': '128'}, 'DIA': {'Enabled': 'true', 'SkyColor': '1 1 1 1', 'SkyIntensity': '1', 'TraceDistance': '128', 'DiffuseSamples': '1', 'ReflectionSamples': str(samples), 'CacheUpdates': '1', 'InternalHeight': '32'}})
            (scenes / f'{name}.scene').write_text('version 2\n\n' + camera + '\n\n' + mesh('Sampling surface', '0 0 0', '512 512 1', material, shape='Quad') + '\n')
            cases[name] = {'angle': angle, 'roughness': roughness, 'reflection_samples': samples, 'expected': expected[angle]}
    hashes = fingerprint(frozen)
    report = {'source_hashes': hashes, 'baseline_hashes': baseline_hashes, 'environment': environment(), 'checks': {}, 'runs': [], 'reference': 'Independent hemisphere quadrature of the GGX BRDF, unit environment and unit metallic Fresnel', 'samples': {'seeds': [911, 1237, 3217], 'frames_per_case': 96, 'extent': [32, 32]}}
    print('Evidence:', output, flush=True)
    for seed in report['samples']['seeds']:
        folder = output / str(seed)
        folder.mkdir()
        events = []
        for index, name in enumerate(cases):
            if index:
                events.append(f'at {index * 96} load "Scenes/DIA/Sampling/{name}.scene"')
            events.append(f'at {index * 96 + 95} capture {name}')
        sequence = folder / 'events.txt'
        sequence.write_text('\n'.join(events) + '\n')
        first = next(iter(cases))
        print('Sampling seed', seed, flush=True)
        report['runs'].append(run(frozen / 'Binaries/Release/Runtime.exe', [f'Scenes/DIA/Sampling/{first}.scene', '--width', 32, '--height', 32, '--frames', len(cases) * 96, '--warmup', 0, '--seed', seed, '--sequence', sequence, '--capture-dir', folder], folder / 'runtime.log'))
    for name, case in cases.items():
        means = [float(read_pfm(output / str(seed) / f'{name}.pfm').mean()) for seed in report['samples']['seeds']]
        maximum = max((float(read_rgba(output / str(seed) / f'{name}.raw1.rgba')[..., :3].max()) for seed in report['samples']['seeds']))
        error = abs(float(np.mean(means)) / case['expected'] - 1)
        report['checks'][name] = {**case, 'status': 'PASS' if error <= 0.025 and maximum <= 1.00001 else 'FAIL', 'mean': float(np.mean(means)), 'relative_error': error, 'maximum_raw_weight': maximum}
        print(name, report['checks'][name], flush=True)
    folder = output / 'cache-budget'
    folder.mkdir()
    sequence = folder / 'events.txt'
    sequence.write_text('at 119 capture before\nat 120 set "Sampling camera" DIA CacheUpdates 2\nat 121 capture changed\nat 159 capture steady\n')
    report['runs'].append(run(frozen / 'Binaries/Release/Runtime.exe', ['Scenes/DIA/Sampling/a0_r70.scene', '--width', 32, '--height', 32, '--frames', 160, '--warmup', 0, '--sequence', sequence, '--capture-dir', folder], folder / 'runtime.log'))
    metadata = [json.loads((folder / f'{stage}.json').read_text()) for stage in ('before', 'changed', 'steady')]
    reset = metadata[1]['scene_revision'] == metadata[0]['scene_revision'] + 1
    stable = metadata[2]['scene_revision'] == metadata[1]['scene_revision']
    report['checks']['cache_budget_edit'] = {'status': 'PASS' if reset and stable else 'FAIL', 'history_invalidated': reset, 'steady_revision': stable}
    assert fingerprint(frozen) == hashes
    if args.baseline:
        assert fingerprint(args.baseline) == baseline_hashes
    report['status'] = 'PASS' if all((c['status'] == 'PASS' for c in report['checks'].values())) else 'FAIL'
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'], 'Evidence:', output, flush=True)
    if report['status'] != 'PASS':
        raise SystemExit(1)
if __name__ == '__main__':
    main()
