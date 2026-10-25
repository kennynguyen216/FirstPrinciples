import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

import numpy as np

from GenerateDIAFixtures import entity, mesh
from ValidateDIA import ROOT, fingerprint, freeze, run
from ValidateDIADynamics import read_rgba
from ValidateDIASampling import expected_energy


def self_check():
    for samples in (1, 8, 64):
        for light_pdf, reflection_pdf in ((.01, 1), (1, 1), (10, .01)):
            light = (4 * light_pdf) ** 2
            reflection = (samples * reflection_pdf) ** 2
            assert np.isclose(light / (light + reflection) + reflection / (light + reflection), 1)
        assert np.isclose(sum(1 / samples for _ in range(samples)), 1)
    assert 16 / 17 != 16 / 80
    assert abs(expected_energy(0, 1) - (1 - np.log(2))) < 1e-5


def direct_fixtures(frozen):
    materials = frozen / 'Content/Materials/DIA'
    (materials / 'ChannelDirectEmitter.material').write_text(
        'Shader "Lit"\nBaseColor 0 0 0 1\nMetallic 1\nRoughness 0.01\nSpecular 0\n'
        'TwoSided true\nEmissiveColor 1 1 1 1\nEmissiveIntensity 1\n')
    cases = {}
    for name, roughness in (('Mirror', .01), ('Rough', .7)):
        material = 'ChannelDirect' + name
        (materials / f'{material}.material').write_text(
            f'Shader "Lit"\nBaseColor 1 1 1 1\nMetallic 1\nRoughness {roughness}\nSpecular 0\n')
        camera = entity('Direct camera', {
            'Name': {'Name': '"Direct camera"'},
            'Transform': {'Position': '0 0 2', 'Rotation': '0 0 0'},
            'Camera': {'Perspective': 'true', 'FieldOfView': '1', 'NearClip': '.1', 'FarClip': '32'},
            'DIA': {'Enabled': 'true', 'SkyIntensity': '0', 'TraceDistance': '128',
                      'DiffuseSamples': '1', 'ReflectionSamples': '1', 'CacheUpdates': '1', 'InternalHeight': '96'}})
        scene = f'Scenes/DIA/{material}.scene'
        blocks = [camera, mesh('Direct receiver', '0 0 0', '4 4 1', material, shape='Quad'),
                  mesh('Direct enclosure', '0 0 0', '8 8 8', 'ChannelDirectEmitter')]
        (frozen / 'Content' / scene).write_text('version 2\n\n' + '\n\n'.join(blocks) + '\n')
        cases[name] = {'scene': scene, 'expected': expected_energy(0, roughness)}
    return cases


def direct_energy(frozen, output, report, cases):
    means = {name: {samples: [] for samples in (1, 8)} for name in cases}
    events = ['at 0 capture Mirror1', 'at 1 set "Direct camera" DIA ReflectionSamples 8',
              'at 1 capture Mirror8', f'at 2 load "{cases["Rough"]["scene"]}"', 'at 2 capture Rough1',
              'at 3 set "Direct camera" DIA ReflectionSamples 8', 'at 3 capture Rough8']
    sequence = output / 'direct.events'
    sequence.write_text('\n'.join(events) + '\n')
    for seed in (911, 1237, 3217):
        folder = output / f'direct-{seed}'
        folder.mkdir()
        run(frozen / 'Binaries/Release/Runtime.exe', [cases['Mirror']['scene'], '--width', 96, '--height', 96,
            '--internal-height', 96, '--frames', 4, '--warmup', 0, '--seed', seed, '--viewmode', 'direct',
            '--sequence', sequence, '--capture-dir', folder], folder / 'runtime.log')
        for name in cases:
            for samples in (1, 8):
                prefix = folder / f'{name}{samples}'
                diffuse = read_rgba(str(prefix) + '.raw0.rgba')
                specular = read_rgba(str(prefix) + '.raw1.rgba')
                exact = read_rgba(str(prefix) + '.raw4.rgba')
                assert np.all(diffuse[..., 3] > 0) and not diffuse[..., :3].any()
                assert np.isfinite(specular).all() and not exact.any()
                means[name][samples].append(float(specular[..., :3].mean()))
    for name, case in cases.items():
        average = {samples: float(np.mean(values)) for samples, values in means[name].items()}
        errors = {samples: abs(value / case['expected'] - 1) for samples, value in average.items()}
        drift = abs(average[8] / average[1] - 1)
        report['checks']['direct_' + name.lower()] = {
            'status': 'PASS' if max(errors.values()) <= .025 and drift <= .025 else 'FAIL',
            'expected': case['expected'], 'seed_means': means[name], 'relative_errors': errors,
            'sample_count_mean_change': drift}
        assert report['checks']['direct_' + name.lower()]['status'] == 'PASS'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    self_check()
    if args.self_check:
        print('PASS direct emission MIS pairing and sample normalization checks')
        return
    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-channels')
    output.mkdir(parents=True)
    frozen, original_hashes = freeze(output)
    cases = direct_fixtures(frozen)
    hashes = fingerprint(frozen)
    shaders = {}
    backups = {}
    for samples, name, anchor in ((1, 'DIATraceLitOne', b'1.0, 4u, 3u,'),
                                  (8, 'DIATrace', b'DIA.Settings.z, 4u, 3u,')):
        shader = frozen / 'src/Engine/Shaders' / (name + '.frag')
        binary = frozen / 'Content/Engine/Shaders' / (name + '.frag.spv')
        metal_path = binary.with_suffix('.msl')
        for path in (shader, binary, metal_path):
            backups[path] = path.read_bytes()
        assert backups[shader].count(anchor) == 1
        shaders[samples] = (shader, binary, metal_path, anchor)
    sequence = output / 'events.txt'
    report = {'original_source_hashes': original_hashes, 'source_hashes': hashes, 'checks': {}, 'status': 'FAIL',
              'direct_scope': 'White metallic receiver inside a two-sided unit-emission enclosure with zero sky. Independent GGX hemisphere quadrature constrains Direct-view energy at one and eight reflection samples. Perfect mirror energy is one.'}
    print('Evidence:', output, flush=True)
    try:
        for samples in (1, 8):
            shader, binary, metal_path, anchor = shaders[samples]
            source = backups[shader]
            sequence.write_text(f'at 0 set "Main Camera" DIA ReflectionSamples {samples}\nat 1 capture sample\n')
            images = {}
            for channels in (3, 1, 2):
                folder = output / f'samples{samples}-channels{channels}'
                folder.mkdir()
                shader.write_bytes(source.replace(anchor, f'{samples}.0, 4u, {channels}u,'.encode()))
                subprocess.run(['glslangValidator', '-V', '--quiet', '-I' + str(shader.parent),
                                '-o', str(binary), str(shader)], check=True)
                subprocess.run([sys.executable, str(frozen / 'Scripts/OptimizeDIAShader.py'), str(binary)], check=True)
                assert isinstance(metal_path, Path)
                subprocess.run(['spirv-cross', str(binary), '--msl', '--msl-version', '20100',
                                '--msl-decoration-binding', '--output', str(metal_path)], check=True)
                run(frozen / 'Binaries/Release/Runtime.exe', ['Scenes/DIA/Review.scene',
                    '--width', 96, '--height', 96, '--internal-height', 96, '--frames', 2, '--warmup', 0,
                    '--seed', 911, '--sequence', sequence, '--capture-dir', folder], folder / 'runtime.log')
                images[channels] = [read_rgba(folder / f'sample.raw{i}.rgba') for i in range(6)]
                assert not images[channels][4][..., 3].any()
            assert all(np.any(images[3][channel][..., :3] > 0) for channel in (0, 1))
            metal = (images[3][3][..., 3] < 0.5) & (images[3][0][..., 3] != 0)
            assert metal.sum() >= 64 and not images[3][0][..., :3][metal].any()
            for channels, requested, omitted in ((1, 0, 1), (2, 1, 0)):
                error = float(np.max(np.abs(images[channels][requested][..., :3] - images[3][requested][..., :3])))
                assert error <= 0.00001, (samples, channels, error)
                assert not images[channels][omitted][..., :3].any()
                assert np.array_equal(images[channels][0][..., 3], images[3][0][..., 3])
                assert np.array_equal(images[channels][2][..., :3], images[3][2][..., :3])
                if channels == 2:
                    assert np.array_equal(images[channels][4], images[3][4])
                    assert np.array_equal(images[channels][5], images[3][5])
                report['checks'][f'samples{samples}-channels{channels}'] = {'status': 'PASS', 'maximum_rgb_error': error}
        for path, data in backups.items():
            path.write_bytes(data)
        direct_energy(frozen, output, report, cases)
        report['status'] = 'PASS'
    finally:
        for path, data in backups.items():
            path.write_bytes(data)
        assert fingerprint(frozen) == hashes
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS independent diffuse and reflection streams', flush=True)


if __name__ == '__main__':
    main()
