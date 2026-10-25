import argparse
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image

from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, region, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, default=ROOT / 'Intermediate/DIAEvidence/20261024-044422-performance/frozen')
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    baseline_hashes = fingerprint(baseline)
    output = ROOT / 'Intermediate/DIAEvidence' / (time.strftime('%Y%m%d-%H%M%S') + '-noise')
    output.mkdir(parents=True)
    frozen, hashes = freeze(output)
    regions = {'left_wall': [.05, .3, .25, .8], 'right_wall': [.78, .3, .95, .8],
               'floor': [.3, .83, .72, .95], 'glossy': [.51, .48, .7, .82]}
    report = {'source_hashes': hashes, 'baseline_hashes': baseline_hashes, 'environment': environment(),
              'settings': {'frames': 600, 'warmup': 300, 'output': [1280, 720], 'internal': [320, 180],
                           'seeds': [911, 1237, 3217], 'regions': regions}, 'regions': {}, 'runs': [],
              'gates': {'noise_ratio': .7, 'mean_change_fraction': .03}, 'independent_review': 'PENDING'}
    images = {'baseline': [], 'candidate': []}
    try:
        for seed in report['settings']['seeds']:
            for label, source in (('baseline', baseline), ('candidate', frozen)):
                print(label, 'seed', seed, flush=True)
                prefix = output / f'{label}-{seed}'
                arguments = ['Scenes/DIA/Review.scene', '--frames', 600, '--warmup', 300, '--width', 1280,
                             '--height', 720, '--seed', seed, '--hdr', prefix.with_suffix('.pfm'),
                             '--metrics', prefix.with_suffix('.json')]
                report['runs'].append(run(source / 'Binaries/Release/Runtime.exe', arguments, prefix.with_suffix('.log')))
                images[label].append(read_pfm(prefix.with_suffix('.pfm')))
                if seed == 911:
                    preview = ['Scenes/DIA/Review.scene', '--frames', 600, '--warmup', 300, '--width', 1280,
                               '--height', 720, '--seed', seed, '--screenshot', prefix.with_suffix('.bmp')]
                    report['runs'].append(run(source / 'Binaries/Release/Runtime.exe', preview, output / f'{label}-preview.log'))
                    Image.open(prefix.with_suffix('.bmp')).save(prefix.with_suffix('.png'))
        for name, bounds in regions.items():
            a, b = (np.stack([region(image, bounds) for image in images[label]]) for label in ('baseline', 'candidate'))
            mean_a, mean_b = (float(luminance(image).mean()) for image in (a, b))
            noise_a, noise_b = (float(np.sqrt(np.mean(np.var(image, axis=0, ddof=1)))) for image in (a, b))
            report['regions'][name] = {'baseline_mean': mean_a, 'candidate_mean': mean_b,
                'mean_change_fraction': abs(mean_b / max(mean_a, 1e-5) - 1),
                'noise_ratio': noise_b / max(noise_a, 1e-5), 'baseline_noise': noise_a, 'candidate_noise': noise_b}
        changes = [value['mean_change_fraction'] for value in report['regions'].values()]
        a = sum(value['baseline_noise'] ** 2 for value in report['regions'].values())
        b = sum(value['candidate_noise'] ** 2 for value in report['regions'].values())
        report['noise_ratio'] = float(np.sqrt(b / max(a, 1e-10)))
        report['status'] = 'PASS' if max(value['noise_ratio'] for value in report['regions'].values()) <= .7 and max(changes) <= .03 else 'FAIL'
        assert fingerprint(frozen) == hashes and fingerprint(baseline) == baseline_hashes
    finally:
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(report.get('status'), report.get('noise_ratio'), report['regions'], flush=True)
        print('Evidence:', output, flush=True)


if __name__ == '__main__':
    main()
