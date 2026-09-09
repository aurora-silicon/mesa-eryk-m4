#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check repeat-wrapped RGBA8 filtering independently of raster interpolation.

Supply raw RGBA8 texture data in GL upload row order. U and V files contain
the float32 bit patterns written into RGBA8 by separate diagnostic draws.
Nearest and linear files contain the sampled texture channel replicated to
RGB, with alpha 255 for coverage and zero for the clear. All files must use
the same pixel order. Captured application assets are external inputs.
Requires NumPy. This checks base-level repeat sampling, not general sampler
conformance or the correctness of the supplied interpolated coordinates.
"""
import argparse
import json
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('texture', 'u', 'v', 'nearest', 'linear'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    parser.add_argument('--channel', type=int, choices=range(4), default=3)
    args = parser.parse_args()
    assert args.width > 0 and args.height > 0
    texture = np.fromfile(args.texture, dtype=np.uint8).reshape(
        args.height, args.width, 4)[:, :, args.channel].astype(np.float64)
    nearest = np.fromfile(args.nearest, dtype=np.uint8).reshape(-1, 4)
    linear = np.fromfile(args.linear, dtype=np.uint8).reshape(-1, 4)
    u = np.fromfile(args.u, dtype='<f4').astype(np.float64)
    v = np.fromfile(args.v, dtype='<f4').astype(np.float64)
    assert len(nearest) == len(linear) == len(u) == len(v)
    covered = nearest[:, 3] == 255
    assert np.array_equal(nearest[:, 3], linear[:, 3])
    assert np.all((nearest[:, 3] == 0) | covered) and covered.any()
    for pixels in (nearest, linear):
        assert np.all(pixels[covered, :3] == pixels[covered, :1])
    u, v = u[covered], v[covered]
    assert np.isfinite(u).all() and np.isfinite(v).all()
    # Repeat before scaling also keeps the integer conversions bounded.
    u = np.remainder(u, 1.0) * args.width
    v = np.remainder(v, 1.0) * args.height
    expected_nearest = texture[np.floor(v).astype(int),
                               np.floor(u).astype(int)]
    nearest_error = np.abs(nearest[covered, 0] - expected_nearest)

    u, v = u - 0.5, v - 0.5
    x, y = np.floor(u).astype(int), np.floor(v).astype(int)
    fx, fy = u - x, v - y

    def sample(dx, dy):
        return texture[(y + dy) % args.height, (x + dx) % args.width]

    expected_linear = (
        (sample(0, 0) * (1 - fx) + sample(1, 0) * fx) * (1 - fy)
        + (sample(0, 1) * (1 - fx) + sample(1, 1) * fx) * fy)
    linear_error = np.abs(linear[covered, 0] - expected_linear)
    result = {
        'pixels': int(covered.sum()),
        'nearest_mismatches': int(np.count_nonzero(nearest_error)),
        'nearest_max_error': float(nearest_error.max()),
        'linear_max_unorm_error': float(linear_error.max()),
        'linear_pixels_over_one_step': int(np.count_nonzero(linear_error > 1)),
    }
    result['passed'] = (result['nearest_mismatches'] == 0
                        and result['linear_pixels_over_one_step'] == 0)
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
