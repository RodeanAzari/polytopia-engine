#!/usr/bin/env python3
"""Simulator speed chart from a `make bench` CSV: decisions/s per config, Tribes vs. ours.

Usage: plot_bench.py <bench.csv> <out.png>
"""

import csv
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SIDES = [("tribes", "Tribes (Java)", "#2a78d6"), ("sim", "Ours (C++)", "#eb6834")]  # categorical slots 1, 2


def main() -> int:
    src, out = sys.argv[1], sys.argv[2]
    steps: dict[tuple[str, str, str], int] = defaultdict(int)
    secs: dict[tuple[str, str, str], float] = defaultdict(float)
    configs: list[tuple[str, str]] = []
    with open(src, newline="") as f:
        for r in csv.DictReader(f):
            key = (r["side"], r["players"], r["mode"])
            steps[key] += int(r["steps"])
            secs[key] += float(r["seconds"])
            if (r["players"], r["mode"]) not in configs:
                configs.append((r["players"], r["mode"]))

    fig, ax = plt.subplots(figsize=(8, 0.75 * len(configs) + 1.4), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)
    h = 0.36
    top = 0.0
    for i, (p, mode) in enumerate(configs):
        for j, (side, _, color) in enumerate(SIDES):
            rate = steps[(side, p, mode)] / secs[(side, p, mode)]
            y = i + (j - 0.5) * (h + 0.04)  # 2px-ish surface gap between the pair
            top = max(top, rate)
            ax.barh(y, rate, height=h, color=color, edgecolor=SURFACE, linewidth=1)
            ax.text(rate, y, f" {rate / 1000:,.0f}k", va="center", ha="left", fontsize=8, color=INK2)
        ratio = (steps[("sim", p, mode)] / secs[("sim", p, mode)]) / (
            steps[("tribes", p, mode)] / secs[("tribes", p, mode)])
        ax.text(1.0, i, f"{ratio:.1f}×", transform=ax.get_yaxis_transform(), va="center", ha="left",
                fontsize=9, color=INK, fontweight="bold")
    ax.set_xlim(0, top * 1.12)  # room for the value labels before the speedup column
    ax.set_yticks(range(len(configs)), [f"{p} players, {m}" for p, m in configs], color=INK, fontsize=9)
    ax.invert_yaxis()
    ax.set_xlabel("decisions per second, one thread (action generation + apply)", color=INK2, fontsize=9)
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
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
