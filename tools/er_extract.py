#!/usr/bin/env python3
"""Extract SPX90 early-reflection tap tables from impulse responses.

The SPX90 runs its DSP at 31.25 kHz. An IR recorded at 44.1 kHz is therefore a sum of
copies of one fixed converter kernel g(t) (input LPF -> A/D -> D/A -> output LPF), shifted
to the 31.25 kHz grid and scaled by the program's tap coefficients:

    h(t) = sum_k a_k * g(t - k*T - tau)

The kernel is taken from the first, isolated reflection of ER1. Every other IR is then
solved for a_k by sparse least squares on that grid, with the sub-sample offset tau and the
recording clock error (ppm) found by search.

Usage:
    er_extract.py extract --sources tools/ir_sources.json --out build/er_fit
    er_extract.py build   --fit build/er_fit --chart tools/chart_taps.json --out src/dsp/ErTables.h
"""
import argparse
import json
import os
import sys

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla
import soundfile as sf

# Measured from tap spacing in two independent captures (SPX90 and SPX90II): the DSP
# runs at 8 MHz / 255 = 31372.55 Hz; the spec sheet's "31.25 kHz" is a rounded figure.
FS_DSP = 8.0e6 / 255.0


def load_mono(path, channel=0):
    x, sr = sf.read(os.path.expanduser(path), always_2d=True)
    return x[:, channel].astype(np.float64), sr


def extract_kernel(er1, sr, pre=4, length_ms=2.4):
    """First reflection of ER1, faded out before the second one starts."""
    peak = int(np.argmax(np.abs(er1[: int(sr * 0.003)])))
    start = max(0, peak - pre)
    n = int(sr * length_ms / 1000)
    k = er1[start:start + n].copy()
    fade = int(n * 0.25)
    k[-fade:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fade)))
    return k / np.abs(k).max(), start


def sinc_sample(x, times, half=48, beta=9.0):
    """Band-limited evaluation of x at fractional sample positions (Kaiser-windowed sinc)."""
    out = np.zeros(len(times))
    offs = np.arange(-half + 1, half + 1)
    win = np.kaiser(2 * half, beta)
    for c in range(0, len(times), 4096):
        t = times[c:c + 4096]
        base = np.floor(t).astype(int)
        idx = base[:, None] + offs[None, :]
        frac = t[:, None] - idx
        w = np.sinc(frac) * win[None, :]
        ok = (idx >= 0) & (idx < len(x))
        out[c:c + 4096] = np.sum(np.where(ok, x[np.clip(idx, 0, len(x) - 1)], 0.0) * w, axis=1)
    return out


def to_grid(x, sr, phase, ppm=0.0):
    """Resample x (rate sr) onto the DSP grid, first grid point at x-sample `phase`."""
    step = sr / FS_DSP * (1 + ppm * 1e-6)
    n = int((len(x) - phase) / step)
    return sinc_sample(x, phase + step * np.arange(n))


def strong_peaks(x, kernel, rel=0.3, up=16):
    """Sub-sample positions (in x samples) of reflections clearly above their surroundings."""
    xu = sinc_sample(x, np.arange(0, len(x) - 1, 1 / up))
    ku = sinc_sample(kernel, np.arange(0, len(kernel) - 1, 1 / up))
    c = np.abs(np.correlate(xu, ku, mode="valid"))
    from scipy.ndimage import maximum_filter1d
    local = maximum_filter1d(c, 2 * up + 1, mode="constant")
    idx = np.flatnonzero((c >= c.max() * rel) & (c == local))
    return idx / up


def grid_alignment(peaks, sr):
    """Clock error and phase that put the strong reflections on integer DSP samples."""
    best = (np.inf, 0.0, 0.0)
    for ppm in np.linspace(-400, 400, 801):
        step = sr / FS_DSP * (1 + ppm * 1e-6)
        n = np.round((peaks - peaks[0]) / step)
        r = peaks - n * step
        phase = np.median(r)
        err = np.sqrt(np.mean((r - phase) ** 2))
        if err < best[0]:
            best = (err, ppm, phase)
    return best


def shift_matrix(g, support, n):
    """Columns are the grid kernel g placed at each tap position in `support`."""
    rows = (np.asarray(support)[:, None] + np.arange(len(g))[None, :]).ravel()
    cols = np.repeat(np.arange(len(support)), len(g))
    vals = np.tile(g, len(support))
    ok = rows < n
    return sp.csc_matrix((vals[ok], (rows[ok], cols[ok])), shape=(n, len(support)))


# Broadband noise of the captures, relative to a full-scale tap. Measured on ER1, whose 13
# taps fit exactly and leave a structureless residual at this level everywhere.
NOISE_DB = -54.0
# Detection threshold in noise standard deviations. 3.5 admits a handful of false taps per
# 10k grid positions but keeps the quiet ends of the dense patterns (down to about -40 dB).
SIGMA_K = 3.5


def omp(y, g, sigma_k=SIGMA_K, noise_db=NOISE_DB, max_taps=4000):
    """Greedy sparse deconvolution: y ~ sum_k a_k g[n-k] with few nonzero a_k.

    Stops when no remaining tap is more than sigma_k standard deviations above what the
    capture noise alone would produce.
    """
    n, gn = len(y), float(np.dot(g, g))
    support, a, r = [], np.zeros(0), y.copy()
    floor = None
    while len(support) < max_taps:
        c = np.correlate(np.concatenate([r, np.zeros(len(g))]), g, mode="valid")[:n] / gn
        if floor is None:
            sigma = np.abs(c).max() * np.abs(g).max() * 10 ** (noise_db / 20)
            floor = sigma_k * sigma / np.sqrt(gn)
        batch = max(4, len(support) // 8)
        taken = set(support)
        picked = []
        for k in np.argsort(-np.abs(c)):
            if abs(c[k]) < floor or len(picked) == batch:
                break
            if k not in taken and all(abs(k - q) > 2 for q in picked):
                picked.append(int(k))
        if not picked:
            break
        support += picked
        x0 = np.concatenate([a, c[picked]])
        A = shift_matrix(g, support, n)
        a = spla.lsqr(A, y, x0=x0, atol=1e-10, btol=1e-10, iter_lim=300)[0]
        r = y - A @ a
    order = np.argsort(support)
    return np.array(support)[order], a[order], float(np.dot(r, r) / np.dot(y, y))


def _unit(n, k):
    e = np.zeros(n)
    e[k] = 1.0
    return e


def onepole_ir(fc, n):
    """Impulse response of the SPX90-style one-pole LPF y += k(x - y) at the DSP rate."""
    k = 1 - np.exp(-2 * np.pi * fc / FS_DSP)
    return k * (1 - k) ** np.arange(n)


def cmd_extract(args):
    cfg = json.load(open(args.sources))
    os.makedirs(args.out, exist_ok=True)
    er1, sr = load_mono(cfg["kernel_source"])
    kernel, kstart = extract_kernel(er1, sr)
    g = to_grid(kernel, sr, 0.0)
    np.save(os.path.join(args.out, "kernel_grid.npy"), g)
    summary = {}
    for name, src in cfg["programs"].items():
        ir, sr2 = load_mono(src["path"], src.get("channel", 0))
        assert sr2 == sr, "all IRs must share the kernel's sample rate"
        ir = ir[: int(sr * src.get("max_ms", 1100) / 1000)]
        peaks = strong_peaks(ir, kernel, src.get("peak_rel", 0.3))
        jitter, ppm, phase = grid_alignment(peaks, sr)
        y = to_grid(ir, sr, phase % (sr / FS_DSP), ppm)
        gk = g
        if "deconvolve_onepole_hz" in src:
            # The program's LPF is baked into this capture; fit taps of the THRU response.
            gk = np.convolve(g, onepole_ir(src["deconvolve_onepole_hz"], 200))[: len(g) + 150]
        support, a, err = omp(y, gk, src.get("sigma_k", SIGMA_K))
        peak = np.abs(a).max()
        taps = [{"n": int(n), "g": float(v / peak)} for n, v in zip(support, a)]
        res = {"ppm": ppm, "grid_jitter": jitter, "n_strong": len(peaks), "residual_db": 10 * np.log10(err),
               "n_taps": len(taps), "peak_scale": float(peak), "settings": src["settings"], "taps": taps}
        json.dump(res, open(os.path.join(args.out, name + ".json"), "w"), indent=1)
        summary[name] = {k: res[k] for k in ("ppm", "grid_jitter", "n_strong", "residual_db", "n_taps")}
        print(f"{name:12s} strong={len(peaks):3d} ppm={ppm:+5.0f} jitter={jitter:.3f} "
              f"taps={len(taps):4d} residual={10*np.log10(err):6.1f} dB")
    json.dump(summary, open(os.path.join(args.out, "summary.json"), "w"), indent=1)


TYPES = ("HALL", "RANDOM", "REVERSE", "PLATE")


def load_taps(fit_dir, name):
    d = json.load(open(os.path.join(fit_dir, name + ".json")))
    n = np.array([t["n"] for t in d["taps"]], dtype=float)
    g = np.array([t["g"] for t in d["taps"]])
    return n - n.min(), g, d["settings"]["room_size"]


def as_sequence(n, g):
    s = np.zeros(int(n.max()) + 1)
    np.add.at(s, n.astype(int), g)
    return s


def sparse_deconvolve(y, k, stop_db):
    """Sparse h with y ~ h * k (both are tap sequences on the DSP grid)."""
    n, kn = len(y), float(np.dot(k, k))
    support, a, r = [], np.zeros(0), y.copy()
    while len(support) < 1000:
        c = np.correlate(np.concatenate([r, np.zeros(len(k))]), k, "valid")[:n] / kn
        j = int(np.argmax(np.abs(c)))
        if j in support:
            break
        support.append(j)
        A = shift_matrix(k, support, n)
        a = spla.lsqr(A, y, x0=np.append(a, c[j]), iter_lim=300)[0]
        r = y - A @ a
        if 10 * np.log10(np.dot(r, r) / np.dot(y, y)) < stop_db:
            break
    h = np.zeros(max(support) + 1)
    h[support] = a
    return h


def chart_pattern(chart, typ, ch):
    """13-tap ER1 pattern from the manual chart (RS 4.0 panels), per unit of room size."""
    taps = chart["patterns"][f"{typ}/4.0/{ch}"]
    pos = np.array([t["ms"] for t in taps]) / 4.0 * FS_DSP / 1000.0
    gain = 10 ** (np.array([t["db"] for t in taps]) / 20)
    return pos, gain


def lr_offset(chart, typ):
    """Chart's right-minus-left tap time offsets per unit room size, as a function of tap time."""
    l, _ = chart_pattern(chart, typ, "L")
    r, _ = chart_pattern(chart, typ, "R")
    m = min(len(l), len(r))
    return l[:m], r[:m] - l[:m]


def min_phase_half(g, n_fft=4096):
    """Minimum-phase FIR whose magnitude is sqrt(|G|): one half of the converter path."""
    G = np.abs(np.fft.fft(g, n_fft))
    log_mag = 0.5 * np.log(np.maximum(G, 1e-6))
    cep = np.fft.ifft(log_mag).real
    fold = np.zeros(n_fft)
    fold[0] = cep[0]
    fold[1:n_fft // 2] = 2 * cep[1:n_fft // 2]
    fold[n_fft // 2] = cep[n_fft // 2]
    h = np.fft.ifft(np.exp(np.fft.fft(fold))).real
    e = np.cumsum(h ** 2) / np.sum(h ** 2)
    n = int(np.searchsorted(e, 1 - 1e-7)) + 1
    return h[:n]


def output_stage(g, h_in, n_fft=4096):
    """The FIR that follows h_in so that the two together are exactly the measured kernel g.

    The measured converter path is not minimum phase; only its magnitude is split evenly, the
    excess phase all goes into the output stage.
    """
    o = np.fft.ifft(np.fft.fft(g, n_fft) / np.fft.fft(h_in, n_fft)).real
    assert np.sum(o[n_fft // 2:] ** 2) < 1e-12 * np.sum(o ** 2), "output stage is not causal"
    e = np.cumsum(o ** 2) / np.sum(o ** 2)
    return o[:int(np.searchsorted(e, 1 - 1e-7)) + 1]


def cmd_build(args):
    chart = json.load(open(args.chart))
    g = np.load(os.path.join(args.fit, "kernel_grid.npy"))
    tables = {}

    # Signs are known only from the HALL ER1 capture; the chart has none. Reuse them.
    n1, g1, rs1 = load_taps(args.fit, "er1_hall")
    strong = np.abs(g1) > 0.1
    signs = np.sign(g1[strong])

    # ER1 HALL L: measured. R: measured L moved by the chart's L->R offsets.
    pl = n1[strong] / rs1
    tl, dl = lr_offset(chart, "HALL")
    tables[("HALL", 1, "L")] = (pl, g1[strong])
    tables[("HALL", 1, "R")] = (pl + np.interp(pl, tl, dl), signs * chart_pattern(chart, "HALL", "R")[1][:len(pl)])
    for typ in TYPES[1:]:
        for ch in "LR":
            pos, gain = chart_pattern(chart, typ, ch)
            tables[(typ, 1, ch)] = (pos, gain * np.resize(signs, len(gain)))

    # ER2: the HALL capture is ER1 HALL convolved with a sparse densifier D.
    n2, g2, rs2 = load_taps(args.fit, "er2_hall")
    D = sparse_deconvolve(as_sequence(n2, g2), as_sequence(n1, g1), stop_db=-30.0)
    D /= np.abs(D).max()
    dpos = np.flatnonzero(D) / rs2
    measured2 = {"HALL": "er2_hall", "RANDOM": "gate_random", "REVERSE": "rev_gate"}
    for typ in TYPES:
        if typ in measured2:
            n, gg, rs = load_taps(args.fit, measured2[typ])
            p = n / rs
            tl, dl = lr_offset(chart, typ)
            tables[(typ, 2, "L")] = (p, gg)
            tables[(typ, 2, "R")] = (p + np.interp(p, tl, dl), gg)
        else:
            for ch in "LR":
                bp, bg = tables[(typ, 1, ch)]
                p = (bp[:, None] + dpos[None, :]).ravel()
                v = (bg[:, None] * D[np.flatnonzero(D)][None, :]).ravel()
                tables[(typ, 2, ch)] = (p, v / np.abs(v).max())

    # Unity gain at 1 kHz: the captured kernel is windowed, so its DC value is not reliable.
    w = 2 * np.pi * 1000.0 / FS_DSP
    g = g / np.abs(np.sum(g * np.exp(-1j * w * np.arange(len(g)))))
    kin = min_phase_half(g)
    kout = output_stage(g, kin)
    write_header(args.out, tables, kin, kout)
    for k, (p, v) in sorted(tables.items()):
        print(f"{k[0]:8s} ER{k[1]} {k[2]}: {len(p):5d} taps, span {p.max() / FS_DSP * 1000:7.2f} ms per RS unit")
    print(f"converter stages: input {len(kin)} taps, output {len(kout)} taps")


def write_header(path, tables, kin, kout):
    out = ["// Generated by tools/er_extract.py build -- do not edit.",
           "// Early-reflection tap tables of the SPX90, positions in DSP samples per unit of ROOM SIZE",
           "// (DSP rate 8 MHz / 255), gains linear and signed, at LIVENESS 5.",
           "#pragma once", "", "namespace spx::tables", "{", "",
           "struct Tap", "{", "    float pos;", "    float gain;", "};", ""]
    names = []
    for (typ, dens, ch), (p, v) in sorted(tables.items()):
        order = np.argsort(p)
        name = f"er{dens}_{typ.lower()}_{ch.lower()}"
        names.append((typ, dens, ch, name, len(p)))
        out.append(f"inline constexpr Tap {name}[] = {{")
        for i in range(0, len(order), 4):
            out.append("    " + " ".join(f"{{{p[j]:.3f}f, {v[j]:+.6f}f}}," for j in order[i:i + 4]))
        out.append("};")
        out.append("")
    out.append("// The measured converter response split into two stages: the input stage (analog LPF + A/D) is")
    out.append("// the minimum-phase half of its magnitude, the output stage (D/A + analog LPF) the rest, so")
    out.append("// that input followed by output reproduces the measured kernel, phase included.")
    for name, k in (("converterIn", kin), ("converterOut", kout)):
        out.append(f"inline constexpr float {name}[] = {{")
        for i in range(0, len(k), 6):
            out.append("    " + " ".join(f"{x:+.9f}f," for x in k[i:i + 6]))
        out.append("};")
        out.append("")
    out.append("} // namespace spx::tables")
    open(path, "w").write("\n".join(out) + "\n")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract")
    e.add_argument("--sources", required=True)
    e.add_argument("--out", required=True)
    b = sub.add_parser("build")
    b.add_argument("--fit", required=True)
    b.add_argument("--chart", required=True)
    b.add_argument("--out", required=True)
    args = p.parse_args()
    {"extract": cmd_extract, "build": cmd_build}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
