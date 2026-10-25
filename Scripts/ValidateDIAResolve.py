import argparse
import json
import time

import numpy as np
from PIL import Image

from GenerateDIAFixtures import entity, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, read_pfm, run


def expected_image(name, size):
    y, x = np.mgrid[:size, :size]
    if name == 'ResolveChecker':
        coordinate_x = (x + 0.5) * 32 / size - 0.5
        coordinate_y = (y + 0.5) * 32 / size - 0.5
        left, top = np.floor(coordinate_x).astype(int), np.floor(coordinate_y).astype(int)
        blend_x, blend_y = coordinate_x - left, coordinate_y - top
        a = (left + top) % 2
        value = ((1 - blend_x) * a + blend_x * (1 - a)) * (1 - blend_y)
        value += ((1 - blend_x) * (1 - a) + blend_x * a) * blend_y
    else:
        distance = 4 if name == 'ResolveThin' else 9
        world_x = (2 * (x + 0.5) / size - 1) * distance
        world_y = (1 - 2 * (y + 0.5) / size) * distance
        if name == 'ResolveThin':
            mask = (abs(world_x - 0.03125) < 0.02) & (abs(world_y) < 1)
        else:
            mask = (abs(world_x - 0.07) < 0.55) & (abs(world_y - 0.1) < 0.55)
        value = mask.astype(float) * 4
    return np.repeat(value[..., None], 3, axis=-1)


def check_image(candidate, expected, silhouette=False):
    assert candidate.shape == expected.shape
    error = np.abs(candidate - expected)
    result = {'status': 'PASS' if error.max() <= 0.001 else 'FAIL',
              'maximum_rgb_error': float(error.max()), 'rgb_rmse': float(np.sqrt(np.mean(error ** 2))),
              'pixels_outside_tolerance': int(np.count_nonzero(error.max(axis=-1) > 0.001))}
    if silhouette:
        observed, reference = candidate.max(axis=-1) > 2, expected.max(axis=-1) > 2
        result['silhouette_mismatches'] = int(np.count_nonzero(observed != reference))
        result['expected_lit_pixels'] = int(reference.sum())
        result['observed_lit_pixels'] = int(observed.sum())
        if result['silhouette_mismatches'] or not reference.any():
            result['status'] = 'FAIL'
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    thin = expected_image('ResolveThin', 128)
    assert np.count_nonzero(thin[..., 0]) == 32
    assert not expected_image('ResolveThin', 64).any()
    assert check_image(thin, thin, True)['status'] == 'PASS'
    assert check_image(np.roll(thin, 1, axis=1), thin, True)['status'] == 'FAIL'
    checker = expected_image('ResolveChecker', 128)
    assert check_image(checker.astype(np.float16).astype(float), checker)['status'] == 'PASS'
    enlarged = np.repeat(np.repeat(expected_image('ResolveChecker', 64), 2, axis=0), 2, axis=1)
    assert check_image(enlarged, checker)['status'] == 'FAIL'
    mirror = expected_image('ResolveMirror', 128)
    assert check_image(mirror, mirror, True)['status'] == 'PASS'
    assert check_image(mirror * 0, mirror, True)['status'] == 'FAIL'
    if args.self_check:
        print('PASS resolve metric self-check')
        return

    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-resolve')
    output.mkdir(parents=True)
    frozen, source_hashes = freeze(output)
    materials = frozen / 'Content/Materials/DIA'
    (materials / 'ResolveEmitter.material').write_text(
        'Shader "Lit"\nBaseColor 0 0 0 1\nSpecular 0\nRoughness 0\nMetallic 0\n'
        'EmissiveColor 1 1 1 1\nEmissiveIntensity 4\nTwoSided false\n')
    (materials / 'ResolveMirror.material').write_text(
        'Shader "Lit"\nBaseColor 1 1 1 1\nMetallic 1\nRoughness 0\nTwoSided false\n')
    (materials / 'ResolveChecker.material').write_text(
        'Shader "Lit"\nBaseColor 1 1 1 1\nSpecular 0\nRoughness 0\nMetallic 0\n'
        'BaseColorMap "Textures/Validation/ResolveChecker.png"\nTwoSided false\n')
    texture = frozen / 'Content/Textures/Validation/ResolveChecker.png'
    texture.parent.mkdir(parents=True, exist_ok=True)
    y, x = np.mgrid[:32, :32]
    Image.fromarray(np.repeat((((x + y) % 2) * 255).astype(np.uint8)[..., None], 3, axis=-1)).save(texture)
    cases = {
        'ResolveThin': [mesh('Thin emitter', '0.03125 0 0', '0.04 2 1', 'ResolveEmitter', shape='Quad')],
        'ResolveChecker': [mesh('Checker receiver', '0 0 0', '8 8 1', 'ResolveChecker', shape='Quad')],
        'ResolveMirror': [mesh('Mirror receiver', '0 0 0', '8 8 1', 'ResolveMirror', shape='Quad'),
                          mesh('Reflected emitter', '0.07 0.1 5', '1.1 1.1 1', 'ResolveEmitter', rotation='0 180 0', shape='Quad')],
    }
    for name, blocks in cases.items():
        camera = entity(name + ' camera', {
            'Name': {'Name': '"Resolve camera"'},
            'Transform': {'Position': '0 0 4', 'Rotation': '0 0 0'},
            'Camera': {'Perspective': 'false' if name == 'ResolveChecker' else 'true',
                       'Zoom': '16', 'FieldOfView': '90', 'NearClip': '0.1', 'FarClip': '128',
                       'Exposure': '1', 'Tonemap': 'true'},
            'DIA': {'Enabled': 'true', 'SkyColor': '1 1 1 1',
                      'SkyIntensity': '1' if name == 'ResolveChecker' else '0', 'TraceDistance': '128',
                      'DiffuseSamples': '1', 'ReflectionSamples': '1', 'CacheUpdates': '4096', 'InternalHeight': '64'},
        })
        (frozen / f'Content/Scenes/DIA/{name}.scene').write_text('version 2\n\n' + '\n\n'.join([camera, *blocks]) + '\n')
    hashes = fingerprint(frozen)
    report = {'source_hashes': source_hashes, 'fixture_hashes': hashes, 'environment': environment(),
              'settings': {'output': [128, 128], 'internal': [64, 64], 'frames': 8, 'seed': 911},
              'absolute_rgb_tolerance': 0.001,
              'analytic_reference': 'Pixel center visibility and wrapped bilinear binary texels. Orthographic normal incidence removes Fresnel reflection from the checker case.',
              'checks': {}, 'runs': []}
    print('Evidence:', output, flush=True)
    for name in cases:
        folder = output / name
        folder.mkdir()
        sequence = folder / 'events.txt'
        sequence.write_text('at 7 capture settled\n')
        report['runs'].append(run(frozen / 'Binaries/Release/Runtime.exe', [
            f'Scenes/DIA/{name}.scene', '--width', 128, '--height', 128, '--internal-height', 64,
            '--frames', 8, '--warmup', 0, '--seed', 911, '--sequence', sequence, '--capture-dir', folder,
        ], folder / 'runtime.log'))
        candidate = read_pfm(folder / 'settled.display.pfm')
        low = read_pfm(folder / 'settled.pfm')
        assert low.shape == (64, 64, 3)
        result = check_image(candidate, expected_image(name, 128), name != 'ResolveChecker')
        result['native'] = check_image(low, expected_image(name, 64), name == 'ResolveMirror')
        if result['native']['status'] != 'PASS':
            result['status'] = 'FAIL'
        if name == 'ResolveThin':
            result['low_resolution_maximum'] = float(np.abs(low).max())
            if result['low_resolution_maximum'] > 0.001:
                result['status'] = 'FAIL'
        report['checks'][name] = result
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name, json.dumps(result), flush=True)
    assert fingerprint(frozen) == hashes
    report['status'] = 'PASS' if all(check['status'] == 'PASS' for check in report['checks'].values()) else 'FAIL'
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'], 'Evidence:', output, flush=True)
    if report['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
