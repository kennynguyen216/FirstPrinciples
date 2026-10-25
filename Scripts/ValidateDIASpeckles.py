import argparse, json, time
from pathlib import Path
import numpy as np
from PIL import Image
from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, region, run
from ValidateDIADynamics import read_rgba

def speckles(image):
    light = luminance(image)
    neighbors = np.lib.stride_tricks.sliding_window_view(np.pad(light, 2, mode='reflect'), (5, 5))
    middle = np.median(neighbors, axis=(-2, -1))
    positive = np.maximum(light - middle, 0)
    return (light, positive, positive > np.maximum(0.02, middle * 0.2))

def receiver_mask(ids, normals):
    neighbors = np.lib.stride_tricks.sliding_window_view(ids, (5, 5))
    normal_neighbors = np.lib.stride_tricks.sliding_window_view(normals[..., :3], (5, 5), axis=(0, 1))
    center = ids[2:-2, 2:-2]
    same = np.all(neighbors == center[..., None, None], axis=(-2, -1))
    aligned = np.all(np.sum(normal_neighbors * normals[2:-2, 2:-2, :3, None, None], axis=2) >= 0.995, axis=(-2, -1))
    mask = np.zeros(ids.shape, dtype=bool)
    mask[2:-2, 2:-2] = same & aligned & (center > 0) & (normals[2:-2, 2:-2, 3] >= 0.5)
    return mask

def self_check():
    flat = np.ones((11, 11, 3), dtype=np.float32)
    assert not speckles(flat)[2].any()
    flat[5, 5] = 2
    assert np.count_nonzero(speckles(flat)[2]) == 1
    assert speckles(flat)[1][5, 5] > 0.99
    ids = np.ones((11, 11))
    normals = np.zeros((11, 11, 4))
    normals[..., 2] = 1
    normals[..., 3] = 0.7
    assert receiver_mask(ids, normals)[5, 5]
    ids[5, 5] = 2
    assert not receiver_mask(ids, normals)[5, 5]
    assert not receiver_mask(ids, normals)[5, 7]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', required=True, type=Path)
    args = parser.parse_args()
    self_check()
    baseline = args.baseline.resolve()
    old_hashes = fingerprint(baseline)
    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-speckles')
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    regions = {'left_wall': [0.01, 0.3, 0.19, 0.8], 'right_wall': [0.81, 0.3, 0.99, 0.8], 'floor': [0.1, 0.9, 0.5, 0.98], 'back_wall': [0.25, 0.28, 0.36, 0.5]}
    report = {'source_hashes': hashes, 'baseline_hashes': old_hashes, 'environment': environment(), 'settings': {'output': [960, 720], 'internal': [240, 180], 'seeds': [911, 1237, 3217], 'captures': {'startup': 120, 'settled': 600}, 'regions': regions}, 'gates': {'aggregate_positive_residual_ratio': 0.5, 'per_region_positive_residual_ratio': 0.7, 'spike_count_ratio': 0.25, 'mean_change': 0.03}, 'checks': {}, 'runs': []}
    images = {label: {stage: [] for stage in ('startup', 'settled')} for label in ('baseline', 'candidate')}
    masks = {label: {stage: [] for stage in ('startup', 'settled')} for label in images}
    print('Evidence:', output, flush=True)
    for seed in report['settings']['seeds']:
        for label, source in (('baseline', baseline), ('candidate', frozen)):
            folder = output / f'{label}-{seed}'
            folder.mkdir()
            sequence = folder / 'events.txt'
            sequence.write_text('at 119 capture startup\nat 599 capture settled\n')
            args_run = ['Scenes/DIA/Review.scene', '--width', 960, '--height', 720, '--internal-height', 180, '--frames', 600, '--warmup', 300, '--seed', seed, '--sequence', sequence, '--capture-dir', folder, '--metrics', folder / 'metrics.json']
            if seed == 911:
                args_run += ['--screenshot', folder / 'preview.bmp']
            print(label, seed, flush=True)
            report['runs'].append(run(source / 'Binaries/Release/Runtime.exe', args_run, folder / 'runtime.log'))
            for stage in images[label]:
                images[label][stage].append(read_pfm(folder / f'{stage}.pfm'))
                masks[label][stage].append(receiver_mask(read_rgba(folder / f'{stage}.raw0.rgba')[..., 3], read_rgba(folder / f'{stage}.raw3.rgba')))
            if seed == 911:
                Image.open(folder / 'preview.bmp').save(folder / 'preview.png')
    for stage in ('startup', 'settled'):
        checks = {}
        total_a = total_b = 0.0
        spikes_a = spikes_b = 0
        for name, bounds in regions.items():
            metrics = {}
            for label in ('baseline', 'candidate'):
                selected = [region(a & b, bounds) for a, b in zip(masks['baseline'][stage], masks['candidate'][stage])]
                assert all((mask.any() for mask in selected))
                values = [tuple((region(x, bounds)[mask] for x in speckles(image))) for image, mask in zip(images[label][stage], selected)]
                metrics[label] = {'mean': float(np.mean([x[0].mean() for x in values])), 'residual_power': float(np.sum([np.sum(x[1] ** 2) for x in values])), 'spikes': int(np.sum([np.count_nonzero(x[2]) for x in values]))}
            a, b = (metrics['baseline'], metrics['candidate'])
            ratio = float(np.sqrt(b['residual_power'] / max(a['residual_power'], 1e-20)))
            change = abs(b['mean'] / a['mean'] - 1)
            checks[name] = {'status': 'PASS' if ratio <= 0.7 and change <= 0.03 else 'FAIL', 'positive_residual_ratio': ratio, 'mean_change': change, 'baseline': a, 'candidate': b}
            total_a += a['residual_power']
            total_b += b['residual_power']
            spikes_a += a['spikes']
            spikes_b += b['spikes']
        ratio = float(np.sqrt(total_b / max(total_a, 1e-20)))
        spike_status = 'PASS' if spikes_b <= max(1, 0.25 * spikes_a) else 'FAIL'
        report['checks'][stage] = {'status': 'PASS' if all((c['status'] == 'PASS' for c in checks.values())) and ratio <= 0.5 and spike_status == 'PASS' else 'FAIL', 'bright_spikes_status': spike_status, 'positive_residual_ratio': ratio, 'spikes_before': spikes_a, 'spikes_after': spikes_b, 'regions': checks}
        print(stage, report['checks'][stage], flush=True)
    assert fingerprint(frozen) == hashes and fingerprint(baseline) == old_hashes
    report['status'] = 'PASS' if all((c['status'] == 'PASS' for c in report['checks'].values())) else 'FAIL'
    (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'], 'Evidence:', output, flush=True)
if __name__ == '__main__':
    main()
