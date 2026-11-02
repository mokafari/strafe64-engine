#!/usr/bin/env python3
"""Colour/exposure metrics + scopes for STRAFE 64 screenshots.

  python3 -I tools/colour/metrics.py report A.jpg [B.jpg ...] -o out_dir [--targets docs/colour-targets.json]
  python3 -I tools/colour/metrics.py compare REF.jpg TEST.jpg -o out_dir

report : per-image metrics (JSON + printed table), a scopes sheet (image, luma waveform,
         RGB parade, luma histogram, Oklab vectorscope, hue histogram), and a pass/fail
         check against the targets file.
compare: the same plus a per-pixel dE_OK heatmap and mean/p95 dE between two shots of the
         SAME view (resized to match).

Conventions: luma Y' = Rec.709 weights on the gamma-encoded sRGB screenshot (what a video
waveform shows, 0..1). Oklab is computed from linearised sRGB; dE_OK is Euclidean distance
in Oklab (x100 so one unit is roughly a just-noticeable difference of ~1-2).
"""
import argparse, json, os, sys
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

W709 = np.array([0.2126, 0.7152, 0.0722])


def load(path, maxw=960):
    im = Image.open(path).convert("RGB")
    if im.width > maxw:
        im = im.resize((maxw, round(im.height * maxw / im.width)), Image.LANCZOS)
    return np.asarray(im).astype(np.float64) / 255.0


def to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def oklab(rgb):
    r, g, b = [to_linear(rgb[..., i]) for i in range(3)]
    l = np.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
    m = np.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
    s = np.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return np.stack([L, a, bb], -1)


def parse_roi(items):
    out = {}
    for it in items or []:
        name, box = it.split("=")
        out[name] = tuple(float(v) for v in box.split(","))  # fractions of w,h: x0,y0,x1,y1
    return out


def stats(rgb, rois=None):
    y = rgb @ W709
    lab = oklab(rgb)
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    C = np.hypot(a, b)
    h = (np.degrees(np.arctan2(b, a)) + 360) % 360
    sh, hi = L < 0.35, L > 0.75
    def mean_ab(mask):
        return (float(a[mask].mean()), float(b[mask].mean())) if mask.any() else (0.0, 0.0)
    sa, sb = mean_ab(sh)
    ha, hb = mean_ab(hi)
    ph = (np.degrees(np.arctan2(sb, sa)) + 360) % 360
    hh = (np.degrees(np.arctan2(hb, ha)) + 360) % 360
    d = {
        "luma_mean": float(y.mean()),
        "luma_p1": float(np.percentile(y, 1)), "luma_p5": float(np.percentile(y, 5)),
        "luma_p50": float(np.percentile(y, 50)), "luma_p95": float(np.percentile(y, 95)),
        "luma_p99": float(np.percentile(y, 99)),
        "dynamic_range_stops_p99_p1": float(np.log2((np.percentile(to_linear(y), 99) + 1e-4) / (np.percentile(to_linear(y), 1) + 1e-4))),
        "clip_pct_any_ch_ge250": float(100 * (rgb.max(-1) >= 250 / 255).mean()),
        "crushed_pct_luma_lt_0.03": float(100 * (y < 0.03).mean()),
        "oklab_L_mean": float(L.mean()), "oklab_chroma_mean": float(C.mean()),
        "oklab_chroma_p95": float(np.percentile(C, 95)),
        "shadow_hue_deg": float(ph), "shadow_chroma": float(np.hypot(sa, sb)),
        "highlight_hue_deg": float(hh), "highlight_chroma": float(np.hypot(ha, hb)),
        "split_tone_hue_gap_deg": float(abs((hh - ph + 180) % 360 - 180)),
        "mean_rgb": [round(float(v * 255)) for v in rgb.reshape(-1, 3).mean(0)],
    }
    H, Wd = y.shape
    d["regions"] = {}
    for n, (x0, y0, x1, y1) in (rois or {}).items():
        sl = (slice(int(y0 * H), int(y1 * H)), slice(int(x0 * Wd), int(x1 * Wd)))
        m = rgb[sl].reshape(-1, 3).mean(0)
        d["regions"][n] = {"hex": "#%02x%02x%02x" % tuple(round(v * 255) for v in m),
                           "luma": float(m @ W709), "oklab_L": float(oklab(m[None, None])[0, 0, 0])}
    if "sky" in d["regions"] and "floor" in d["regions"]:
        d["sky_to_floor_luma_ratio"] = d["regions"]["floor"]["luma"] / max(d["regions"]["sky"]["luma"], 1e-4)
    return d, y, lab, C, h


def scopes(path, rgb, y, lab, C, h, out):
    fig = plt.figure(figsize=(15, 8.5), facecolor="#111")
    gs = fig.add_gridspec(2, 3, hspace=0.28, wspace=0.18)
    def ax_(i, j, title):
        ax = fig.add_subplot(gs[i, j]); ax.set_facecolor("#111"); ax.set_title(title, color="#ddd", fontsize=10)
        ax.tick_params(colors="#999", labelsize=7)
        for s in ax.spines.values(): s.set_color("#444")
        return ax
    ax = ax_(0, 0, path); ax.imshow(rgb); ax.axis("off")
    # luma waveform: x = image column, y = luma, density
    ax = ax_(0, 1, "Luma waveform (Y' 0..1)")
    H, Wd = y.shape
    xs = np.repeat(np.arange(Wd)[None, :], H, 0).ravel() / Wd
    ax.hist2d(xs, y.ravel(), bins=[240, 128], range=[[0, 1], [0, 1]], cmap="viridis", norm=matplotlib.colors.LogNorm())
    for v, c in ((0.03, "#c44"), (0.98, "#c44")): ax.axhline(v, color=c, lw=0.6, ls="--")
    # RGB parade
    ax = ax_(0, 2, "RGB parade")
    for k, col in enumerate(("#f55", "#5f5", "#58f")):
        ax.hist2d(xs / 3 + k / 3, rgb[..., k].ravel(), bins=[120, 128], range=[[k / 3, (k + 1) / 3], [0, 1]],
                  cmap=matplotlib.colors.LinearSegmentedColormap.from_list("c", ["#111", col]), norm=matplotlib.colors.LogNorm())
    ax.set_xlim(0, 1)
    # luma histogram (log y) with percentile markers
    ax = ax_(1, 0, "Luma histogram (log) + P1/P50/P99")
    ax.hist(y.ravel(), bins=128, range=(0, 1), color="#8ab", log=True)
    for p, c in ((1, "#fa5"), (50, "#fff"), (99, "#fa5")): ax.axvline(np.percentile(y, p), color=c, lw=0.8)
    # Oklab vectorscope (a-b plane), hue ring
    ax = ax_(1, 1, "Oklab vectorscope (a,b)")
    ax.hist2d(lab[..., 1].ravel(), lab[..., 2].ravel(), bins=160, range=[[-0.25, 0.25], [-0.25, 0.25]],
              cmap="magma", norm=matplotlib.colors.LogNorm())
    t = np.linspace(0, 2 * np.pi, 200)
    for r in (0.05, 0.1, 0.15): ax.plot(r * np.cos(t), r * np.sin(t), color="#555", lw=0.4)
    for deg, nm in ((0, "red/pink"), (60, "orange"), (110, "yellow-grn"), (200, "cyan"), (260, "blue"), (320, "purple")):
        ax.text(0.2 * np.cos(np.radians(deg - 15 if deg == 0 else deg)), 0.2 * np.sin(np.radians(deg)), nm, color="#999", fontsize=6, ha="center")
    ax.set_aspect("equal")
    # hue histogram weighted by chroma
    ax = ax_(1, 2, "Hue distribution (weighted by chroma), Oklch deg")
    ax.hist(h.ravel(), bins=72, range=(0, 360), weights=C.ravel(), color="#c9a")
    ax.set_xlim(0, 360)
    fig.savefig(out, dpi=90, facecolor=fig.get_facecolor(), bbox_inches="tight")
    plt.close(fig)


def check(d, targets):
    rows = []
    for k, rule in (targets.get("global") or {}).items():
        v = d.get(k)
        if v is None: continue
        lo, hi = rule.get("min", -1e9), rule.get("max", 1e9)
        rows.append((k, v, lo, hi, lo <= v <= hi))
    for rn, rr in (targets.get("regions") or {}).items():
        for k, rule in rr.items():
            v = (d["regions"].get(rn) or {}).get(k)
            if v is None: continue
            lo, hi = rule.get("min", -1e9), rule.get("max", 1e9)
            rows.append((f"{rn}.{k}", v, lo, hi, lo <= v <= hi))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["report", "compare"])
    ap.add_argument("images", nargs="+")
    ap.add_argument("-o", "--out", default="colour_out")
    ap.add_argument("--targets")
    ap.add_argument("--roi", action="append", help="name=x0,y0,x1,y1 as fractions, e.g. sky=0.3,0.05,0.7,0.25")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rois = parse_roi(a.roi)
    targets = json.load(open(a.targets)) if a.targets else {}
    results = {}
    loaded = {}
    for p in a.images:
        rgb = load(p)
        d, y, lab, C, h = stats(rgb, rois)
        # label = parent dir + file stem, so the same shot from different rounds stays distinct
        label = os.path.basename(os.path.dirname(os.path.abspath(p))) + "_" + os.path.splitext(os.path.basename(p))[0]
        results[label] = d
        loaded[p] = (rgb, lab)
        scopes(label, rgb, y, lab, C, h, os.path.join(a.out, label + "_scopes.png"))
    if a.mode == "compare":
        (r1, l1), (r2, l2) = loaded[a.images[0]], loaded[a.images[1]]
        if r1.shape != r2.shape:
            r2i = np.asarray(Image.fromarray((r2 * 255).astype(np.uint8)).resize((r1.shape[1], r1.shape[0]), Image.LANCZOS)).astype(float) / 255
            l2 = oklab(r2i)
        dE = 100 * np.linalg.norm(l1 - l2, axis=-1)
        results["_compare"] = {"dE_OK_mean": float(dE.mean()), "dE_OK_p95": float(np.percentile(dE, 95)),
                               "dL_mean_x100": float(100 * (l2[..., 0] - l1[..., 0]).mean())}
        fig, ax = plt.subplots(figsize=(9, 5)); im = ax.imshow(dE, cmap="inferno", vmin=0, vmax=40); ax.axis("off")
        fig.colorbar(im, label="dE_OK (x100)"); fig.savefig(os.path.join(a.out, "dE_heatmap.png"), dpi=90, bbox_inches="tight"); plt.close(fig)
    json.dump(results, open(os.path.join(a.out, "metrics.json"), "w"), indent=1)
    keys = ["luma_p1", "luma_p50", "luma_p99", "clip_pct_any_ch_ge250", "crushed_pct_luma_lt_0.03", "oklab_chroma_mean",
            "shadow_hue_deg", "highlight_hue_deg", "split_tone_hue_gap_deg", "sky_to_floor_luma_ratio"]
    print("%-34s" % "metric" + "".join("%-26s" % n[-24:] for n in results if not n.startswith("_")))
    for k in keys:
        print("%-34s" % k + "".join("%-26s" % ("%.3f" % r[k] if k in r else "-") for n, r in results.items() if not n.startswith("_")))
    if "_compare" in results: print("compare:", results["_compare"])
    if targets:
        print("\nTARGET CHECK")
        for n, d in results.items():
            if n.startswith("_"): continue
            fails = [(k, v, lo, hi) for k, v, lo, hi, ok in check(d, targets) if not ok]
            print(f"  {n}: {'PASS' if not fails else 'FAIL'}")
            for k, v, lo, hi in fails: print(f"      {k} = {v:.3f}  (want {lo:g}..{hi:g})")


if __name__ == "__main__":
    main()
