from pathlib import Path
import re

import numpy as np


def check_mirror_seed_skip():
    source = (Path(__file__).resolve().parents[1] / 'src/Engine/Shaders/DIAShared.glsl').read_text()
    multiplier, increment = map(int, re.search(r'state = state \* (\d+)u \+ (\d+)u;', source).groups())
    jump_multiplier, jump_increment = map(int, re.search(r'seed = seed \* (\d+)u \+ (\d+)u;', source).groups())
    rng = np.random.default_rng(935)
    seeds = np.r_[np.array([0, 1, 2, 0x7fffffff, 0xffffffff], dtype=np.uint64),
                  rng.integers(0, 2 ** 32, 65536, dtype=np.uint64)]
    expected = seeds.copy()
    for _ in range(16):
        expected = (expected * multiplier + increment) & 0xffffffff
    actual = (seeds * jump_multiplier + jump_increment) & 0xffffffff
    assert np.array_equal(actual, expected)
    print('PASS mirror shortcut preserves all sixteen area-sample draws')


def probabilities(cdf, uniform):
    total = float(cdf[-1])
    if total <= 0:
        return np.full(len(cdf), 1 / len(cdf))
    return uniform / len(cdf) + (1 - uniform) * np.diff(np.r_[0., cdf]) / total


def strata_probabilities(cdf, uniform):
    count, total = len(cdf), float(cdf[-1])
    edges = np.r_[0., cdf] / total if total > 0 else np.linspace(0, 1, count + 1)
    uniform = uniform if total > 0 else 1.
    result = []
    for stratum in range(4):
        low, high = stratum / 4, (stratum + 1) / 4
        def overlap(values):
            return 4 * np.maximum(0, np.minimum(values[1:], high) - np.maximum(values[:-1], low))
        result.append(uniform * overlap(np.linspace(0, 1, count + 1)) + (1 - uniform) * overlap(edges))
    return np.asarray(result)


def selections(cdf, stratified, rng, draws):
    count, total = len(cdf), float(cdf[-1])
    uniform = .05 if total > 0 else 1.
    coin = rng.random((draws, 4), dtype=np.float32)
    u = rng.random((draws, 4), dtype=np.float32)
    if stratified:
        u = np.minimum((u + np.arange(4, dtype=np.float32)) * .25, np.float32(.99999994))
    chosen = np.minimum((u * np.float32(count)).astype(int), count - 1)
    weighted = np.minimum(np.searchsorted(cdf, u * np.float32(total), side='right'), count - 1)
    return np.where(coin < uniform, chosen, weighted)


def estimate(cdf, values, stratified, seed, draws=131072):
    rng = np.random.default_rng(seed)
    chosen = selections(cdf, stratified, rng, draws)
    p = probabilities(cdf.astype(float), .05 if cdf[-1] > 0 else 1.)
    return (values[chosen] / p[chosen]).mean(axis=1)


def check_mean(values, expected):
    error = abs(values.mean() - expected)
    standard_error = values.std(ddof=1) / np.sqrt(len(values))
    assert error <= 4 * standard_error + .000001, (values.mean(), expected, standard_error)


def rectangle_estimate(stratified, seed, draws=131072):
    a, b, height = 1.3, .7, .8
    cdf = np.array([1, 2], dtype=np.float32)
    rng = np.random.default_rng(seed)
    chosen = selections(cdf, stratified, rng, draws)
    vertices = np.array([[[-a, -b, height], [a, b, height], [a, -b, height]],
                         [[-a, -b, height], [-a, b, height], [a, b, height]]])
    root = np.sqrt(rng.random((draws, 4)))
    v = rng.random((draws, 4))
    barycentric = np.stack((1 - root, v * root, (1 - v) * root), axis=-1)
    points = np.sum(vertices[chosen] * barycentric[..., None], axis=-2)
    distance_squared = np.sum(points * points, axis=-1)
    samples = (2 * a * b) * height ** 2 / (np.pi * .5 * distance_squared ** 2)
    expected = (2 / np.pi) * (a / np.sqrt(a * a + height * height) * np.arctan(b / np.sqrt(a * a + height * height)) +
                              b / np.sqrt(b * b + height * height) * np.arctan(a / np.sqrt(b * b + height * height)))
    nodes, weights = np.polynomial.legendre.leggauss(64)
    distance = (nodes[:, None] * a) ** 2 + (nodes[None, :] * b) ** 2 + height ** 2
    integral = float(np.sum(height ** 2 / distance ** 2 * weights[:, None] * weights[None, :]) * a * b / np.pi)
    assert abs(integral - expected) < 1e-10
    return samples.mean(axis=1), expected


def main():
    check_mirror_seed_skip()
    rng = np.random.default_rng(17)
    pixels = np.array([[0, 0], [1, 0], [0, 1], [1, 1]])
    for phase in range(4):
        cells = (((pixels[:, 0] & 1) + 2 * (pixels[:, 1] & 1)) ^ phase) & 3
        assert sorted(cells) == list(range(4))
        assert np.array_equal(cells[:2] & 1, cells[2:] & 1)
        assert np.array_equal(cells[[0, 2]] >> 1, cells[[1, 3]] >> 1)
    for pixel in ((0, 0), (17, 8), (239, 179)):
        cells = (((pixel[0] & 1) + 2 * (pixel[1] & 1)) ^ np.arange(4)) & 3
        lower = np.stack((cells % 2, cells // 2), axis=-1) * .5
        upper = lower + .5
        np.testing.assert_allclose(((lower + upper) / 2).mean(axis=0), [.5, .5])
        np.testing.assert_allclose(((lower ** 2 + lower * upper + upper ** 2) / 3).mean(axis=0), [1 / 3, 1 / 3])
        endpoint = np.minimum((lower.astype(np.float32) * 2 + np.float32(1 - 2 ** -24)) * .5, np.float32(.99999994))
        assert np.all((endpoint >= 0) & (endpoint < 1))
    for count in range(1, 65):
        columns = max(divisor for divisor in range(1, 9) if divisor * divisor <= count and count % divisor == 0)
        rows = count // columns
        for offset in range(count):
            cells = (np.arange(count) + offset) % count
            assert len(set(zip(cells % columns, cells // columns))) == count
        cells = np.arange(count)
        lower = np.stack((cells % columns / columns, cells // columns / rows), axis=-1)
        upper = lower + [1 / columns, 1 / rows]
        np.testing.assert_allclose(((lower + upper) / 2).mean(axis=0), [.5, .5], atol=1e-15)
        np.testing.assert_allclose(((lower ** 2 + lower * upper + upper ** 2) / 3).mean(axis=0), [1 / 3, 1 / 3], atol=1e-15)
        endpoint = np.minimum((np.array([columns - 1, rows - 1], dtype=np.float32) + np.float32(1 - 2 ** -24)) /
                              np.array([columns, rows], dtype=np.float32), np.float32(.99999994))
        assert np.all((endpoint >= 0) & (endpoint < 1))
    for count in (1, 12, 127, 262144):
        weights = np.exp(rng.uniform(-12, 2, count))
        weights[rng.random(count) < .6] = 0
        if count == 262144:
            weights[:] = 0
            weights[12345] = 1
        cdf = np.cumsum(weights).astype(np.float32)
        uniform = .05 if cdf[-1] > 0 else 1.
        p = probabilities(cdf.astype(float), uniform)
        np.testing.assert_allclose(strata_probabilities(cdf.astype(float), uniform).sum(axis=0), 4 * p, rtol=1e-8, atol=1e-14)
        for stratum in range(4):
            u = np.minimum((np.float32(stratum) + np.float32(1 - 2 ** -24)) * np.float32(.25), np.float32(.99999994))
            assert u < 1 and min(count - 1, int(u * count)) < count
    cases = {'cube_visibility': (np.cumsum(np.ones(12)).astype(np.float32), np.r_[np.zeros(8), [10, 10], np.zeros(2)]),
             'safety_floor': (np.cumsum([0, 0, 1, 0, 3, 0, 0, 2]).astype(np.float32), np.array([1, 0, 2, 0, 3, 0, 1, 1])),
             'zero_total': (np.zeros(12, dtype=np.float32), np.arange(12, dtype=float))}
    for name, (cdf, values) in cases.items():
        old = estimate(cdf, values, False, 7919)
        new = estimate(cdf, values, True, 104729)
        p = probabilities(cdf.astype(float), .05 if cdf[-1] > 0 else 1.)
        conditional = strata_probabilities(cdf.astype(float), .05)
        contribution = values / p
        old_variance = (np.sum(p * contribution ** 2) - values.sum() ** 2) / 4
        new_variance = np.sum(conditional @ (contribution ** 2) - (conditional @ contribution) ** 2) / 16
        assert new_variance <= old_variance + 1e-10
        check_mean(old, values.sum())
        check_mean(new, values.sum())
        print(name, 'means', old.mean(), new.mean(), 'target', values.sum(), 'variance ratio', new.var(ddof=1) / old.var(ddof=1))
    for stratified in (False, True):
        values, expected = rectangle_estimate(stratified, 911 if stratified else 1237)
        check_mean(values, expected)
        print('rectangle', stratified, 'mean', values.mean(), 'analytic', expected)
    print('PASS stratified mixture, safety floor, variance and analytic irradiance')


if __name__ == '__main__':
    main()
