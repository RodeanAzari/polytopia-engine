#!/usr/bin/env python3
"""Simulator speed chart from one or more `make bench` CSVs (repeat runs of the same games): decisions/s per
config, Tribes vs. ours. Bars are the median over runs, whiskers the min-max; the speedup column is the median
of the per-run speedups with its range. Also prints that summary as a Markdown table.

Usage: plot_bench.py <out.png> <bench.csv>...
"""

import csv
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SIDES = [("tribes", "Tribes (Java)", "#2a78d6"), ("sim", "Ours (C++)", "#eb6834")]  # categorical slots 1, 2


def load(path: str) -> dict[tuple[str, str, str], float]:
    """(side, players, mode) -> aggregate decisions/s in one run."""
    steps: dict[tuple[str, str, str], int] = defaultdict(int)
    secs: dict[tuple[str, str, str], float] = defaultdict(float)
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            key = (r["side"], r["players"], r["mode"])
            steps[key] += int(r["steps"])
            secs[key] += float(r["seconds"])
    return {k: steps[k] / secs[k] for k in steps}


def main() -> int:
    out, runs = sys.argv[1], [load(p) for p in sys.argv[2:]]
    configs = list(dict.fromkeys((p, m) for (_, p, m) in runs[0]))
    configs.sort(key=lambda c: (c[1], int(c[0])))
    if any(set(r) != set(runs[0]) for r in runs):
        raise SystemExit("plot_bench: runs cover different configs")

    fig, ax = plt.subplots(figsize=(8, 0.75 * len(configs) + 1.6), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)
    h, top = 0.36, 0.0
    print("| players | mode | Tribes decisions/s | ours decisions/s | speedup |")
    print("|---|---|---|---|---|")
    for i, (p, mode) in enumerate(configs):
        med = {}
        for j, (side, _, color) in enumerate(SIDES):
            rates = [r[(side, p, mode)] for r in runs]
            med[side] = statistics.median(rates)
            top = max(top, max(rates))
            y = i + (j - 0.5) * (h + 0.04)
            ax.barh(y, med[side], height=h, color=color, edgecolor=SURFACE, linewidth=1)
            if len(runs) > 1:
                ax.plot([min(rates), max(rates)], [y, y], color=INK, linewidth=1, solid_capstyle="butt")
            ax.text(max(rates), y, f" {med[side] / 1000:,.0f}k", va="center", ha="left", fontsize=8, color=INK2)
        ratios = [r[("sim", p, mode)] / r[("tribes", p, mode)] for r in runs]
        rng = f"{min(ratios):.1f}–{max(ratios):.1f}" if len(runs) > 1 else ""
        ax.text(1.0, i, f"{statistics.median(ratios):.1f}×", transform=ax.get_yaxis_transform(), va="center",
                ha="left", fontsize=9, color=INK, fontweight="bold")
        if rng:
            ax.text(1.0, i + 0.3, rng, transform=ax.get_yaxis_transform(), va="center", ha="left", fontsize=7,
                    color=INK2)
        span = f" ({rng})" if rng else ""
        print(f"| {p} | {mode} | {med['tribes']:,.0f} | {med['sim']:,.0f} | {statistics.median(ratios):.1f}x{span} |")
    ax.set_xlim(0, top * 1.14)  # room for the value labels before the speedup column
    ax.set_yticks(range(len(configs)), [f"{p} players, {m}" for p, m in configs], color=INK, fontsize=9)
    ax.invert_yaxis()
    runs_note = f"median of {len(runs)} runs, whiskers min–max" if len(runs) > 1 else "one run"
    ax.set_xlabel(f"decisions per second, one thread (action generation + apply); {runs_note}", color=INK2,
                  fontsize=9)
    ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v / 1000:,.0f}k"))
    ax.tick_params(colors=INK2, labelsize=8, length=0)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_title("Random-playout speed: our simulator vs. Tribes' forward model", color=INK, fontsize=11,
                 loc="left", pad=22)
    handles = [matplotlib.patches.Patch(color=c, label=lbl) for _, lbl, c in SIDES]
    ax.legend(handles=handles, loc="lower left", bbox_to_anchor=(0, 1.0), ncol=2, frameon=False, fontsize=8,
              labelcolor=INK2, borderaxespad=0.2)
    ax.text(1.0, 1.02, "speedup", transform=ax.transAxes, ha="left", va="bottom", fontsize=8, color=INK2)
    fig.tight_layout()
    fig.savefig(out, facecolor=SURFACE)
    print(f"wrote {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
