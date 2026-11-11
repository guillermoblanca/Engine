"""Measures the snow fields the `GBSimTests terrain_graph_cache snow` probe writes (tests/snow_probe.inl; T112). Needs numpy.

usage: snow_metrics.py <dir> <tag> [variant ...]   (tag: <graph>_<type>_<seed>_<resolution>)
For each variant (<tag>_snow_<variant>.f32):
  cover   mean weight over the map (%), and share of samples > 0.5
  partial share of samples with 0.01 < w < 0.99
  ext4/8  share of partial samples that are strict local extrema over their 4 / 8 neighbours (%); nExt4 their count
  axis    anisotropy of the high-passed field's power spectrum: mean power in +-2 deg wedges around 0/45/90/135 deg over the mean power in
          the other directions (1 = isotropic), on a Hann-windowed crop around the snow, wavelengths 2..16 samples
  A8/A4   gradient-orientation harmonics over the partial samples (Scharr, weighted by |grad|): A8 = alignment to the 8 grid directions
          (0 isotropic, 1 all along them), A4 > 0 axes / < 0 diagonals; the height's own is printed as the floor
  line m  snowline: height of the 10th percentile of the samples > 0.5
"""
import sys, json, os
import numpy as np

sys.stdout.reconfigure(encoding='utf-8')


def load(path, n):
    return np.fromfile(path, dtype=np.float32).reshape(n, n).astype(np.float64)


def extrema_share(w, lo=0.01, hi=0.99, four=False):
    n = w.shape[0]
    c = w[1:-1, 1:-1]
    nb = [w[1 + dj:n - 1 + dj, 1 + di:n - 1 + di] for dj in (-1, 0, 1) for di in (-1, 0, 1) if (di or dj) and not (four and di and dj)]
    mx = np.ones_like(c, dtype=bool)
    mn = np.ones_like(c, dtype=bool)
    for b in nb:
        mx &= c > b
        mn &= c < b
    partial = (c > lo) & (c < hi)
    k = partial.sum()
    return 100.0 * ((mx | mn) & partial).sum() / max(k, 1), k


def crop_box(w):
    js, is_ = np.nonzero((w > 0.01) & (w < 0.99))
    if len(js) == 0:
        return None
    return js.min(), js.max() + 1, is_.min(), is_.max() + 1


def axis_ratio(w, box, kmin=2.0, kmax=16.0, wedge=2.0):
    j0, j1, i0, i1 = box
    a = w[j0:j1, i0:i1].copy()
    # high pass: minus a 9x9 box blur, so the broad cover does not dominate
    from numpy.lib.stride_tricks import sliding_window_view
    pad = np.pad(a, 4, mode='edge')
    blur = sliding_window_view(pad, (9, 9)).mean(axis=(2, 3))
    a = a - blur
    a = a - a.mean()
    hj = np.hanning(a.shape[0])[:, None]
    hi = np.hanning(a.shape[1])[None, :]
    a = a * hj * hi
    F = np.fft.fftshift(np.abs(np.fft.fft2(a)) ** 2)
    fy = np.fft.fftshift(np.fft.fftfreq(a.shape[0]))[:, None]
    fx = np.fft.fftshift(np.fft.fftfreq(a.shape[1]))[None, :]
    r = np.sqrt(fx ** 2 + fy ** 2)
    ang = (np.degrees(np.arctan2(fy, fx)) + 180.0) % 180.0
    band = (r >= 1.0 / kmax) & (r <= 1.0 / kmin)
    d = np.minimum.reduce([np.abs(((ang - t) + 90.0) % 180.0 - 90.0) for t in (0.0, 45.0, 90.0, 135.0)])
    on = band & (d <= wedge)
    off = band & (d > wedge + 3.0)
    return F[on].mean() / max(F[off].mean(), 1e-30)


def harmonics(w, mask):
    """|sum m e^{i k theta}| / sum m of the gradient orientation (Scharr), weighted by |grad|, over `mask`: A8 = alignment to the 8 grid
    directions, A4 = to the axes (+) or the diagonals (-, the sign of the real part)."""
    p = np.pad(w, 1, mode='edge')
    gx = (3 * (p[:-2, 2:] - p[:-2, :-2]) + 10 * (p[1:-1, 2:] - p[1:-1, :-2]) + 3 * (p[2:, 2:] - p[2:, :-2])) / 32.0
    gy = (3 * (p[2:, :-2] - p[:-2, :-2]) + 10 * (p[2:, 1:-1] - p[:-2, 1:-1]) + 3 * (p[2:, 2:] - p[:-2, 2:])) / 32.0
    m = np.hypot(gx, gy) * mask
    th = np.arctan2(gy, gx)
    s = max(m.sum(), 1e-30)
    a8 = np.abs((m * np.exp(8j * th)).sum()) / s
    a4 = (m * np.exp(4j * th)).sum() / s
    return a8, a4.real


def main():
    d, tag = sys.argv[1], sys.argv[2]
    meta = json.load(open(os.path.join(d, tag + '_snow.json')))
    n = meta['n']
    h = load(os.path.join(d, tag + '_height.f32'), n)
    variants = sys.argv[3:] or ['old', 'node']
    base = load(os.path.join(d, tag + '_snow_old.f32'), n)
    box = crop_box(base)
    print(f"{tag}: n {n}, node {meta['node_ms']:.0f} ms, old {meta['old_ms']:.0f} ms, crop {box}")
    hn = h / max(np.ptp(h), 1e-6)
    pb = (base > 0.01) & (base < 0.99)
    ha8, ha4 = harmonics(hn, pb)
    print(f"  height A8 {ha8:.3f} A4 {ha4:+.3f} over the old partial samples")
    print(f"  height field axis ratio {axis_ratio(h / max(np.ptp(h), 1e-6), box):.2f} (reference: the terrain's own)")
    print(f"  {'variant':22s} {'cover%':>7s} {'>0.5%':>7s} {'partial%':>8s} {'ext4%':>6s} {'ext8%':>6s} {'axis':>6s} {'A8':>6s} {'A4':>6s} {'nExt4':>6s} {'line m':>7s}")
    for v in variants:
        p = os.path.join(d, f'{tag}_snow_{v}.f32')
        if not os.path.exists(p):
            print(f'  {v}: missing')
            continue
        w = load(p, n)
        ext, k = extrema_share(w)
        ext4, _ = extrema_share(w, four=True)
        full = w > 0.5
        part = (w > 0.01) & (w < 0.99)
        a8, a4 = harmonics(w, part)
        n4 = ext4 * k / 100.0
        line = np.percentile(h[full], 10) if full.any() else float('nan')
        print(f"  {v:22s} {100 * w.mean():7.3f} {100 * full.mean():7.3f} {100 * k / w.size:8.3f} {ext4:6.2f} {ext:6.2f} {axis_ratio(w, box):6.2f} {a8:6.3f} {a4:+6.3f} {n4:6.0f} {line:7.1f}")


main()
