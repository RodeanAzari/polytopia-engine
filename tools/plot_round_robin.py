#!/usr/bin/env python3
"""Baseline round-robin chart (ADR-007 §6): for every agent pair, the row agent's win rate with its Wilson 95%
interval, our simulator vs. Tribes, one row per pair. Also prints the ADR-007 §6 criteria check.

Usage: plot_round_robin.py <out.png> <ours.csv> <tribes.csv>
  ours.csv:   tools/tournament output (agent0, agent1, result0)
  tribes.csv: oracle/scripts/tribes_tournament.py output (seat0, seat1, result0)
"""

import csv
import itertools
import math
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SIDES = [("tribes", "Tribes (Java), 50 games/pair", "#2a78d6"), ("ours", "Ours (C++), 500 games/pair", "#eb6834")]
AGENTS = ["RuleBased", "RHEA", "MCTS", "OSLA", "MC", "Random"]
NAMES = {"Rule Based": "RuleBased"}


def wilson(w: float, n: int, z: float = 1.96) -> tuple[float, float]:
    p = w / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return (c - h) / d, (c + h) / d


def tally(path: str, a0: str, a1: str) -> dict[tuple[str, str], tuple[float, int]]:
    """(row agent, column agent) -> (wins + draws/2, games), both orientations."""
    t: dict[tuple[str, str], tuple[float, int]] = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            x, y = NAMES.get(r[a0], r[a0]), NAMES.get(r[a1], r[a1])
            s = {"WIN": 1.0, "DRAW": 0.5}.get(r["result0"], 0.0)
            for a, b, v in ((x, y, s), (y, x, 1 - s)):
                w, n = t.get((a, b), (0.0, 0))
                t[(a, b)] = (w + v, n + 1)
    return t


def direction(lo: float, hi: float) -> int:
    return 1 if lo > 0.5 else -1 if hi < 0.5 else 0


def main() -> int:
    out = sys.argv[1]
    data = {"ours": tally(sys.argv[2], "agent0", "agent1"), "tribes": tally(sys.argv[3], "seat0", "seat1")}
    pairs = list(itertools.combinations(AGENTS, 2))  # row agent = the stronger one in Tribes' ordering

    ok = True
    print("| pair | ours (N; 95% CI) | Tribes (N; 95% CI) | §6.2 direction | §6.3 overlap |")
    print("|---|---|---|---|---|")
    for a, b in pairs:
        cells, iv = [], {}
        for side, _, _ in SIDES[::-1]:
            w, n = data[side][(a, b)]
            iv[side] = wilson(w, n)
            cells.append(f"{w / n:.1%} ({n}; {iv[side][0]:.1%}–{iv[side][1]:.1%})")
        td, od = direction(*iv["tribes"]), direction(*iv["ours"])
        c2 = not (td and od == -td)
        c3 = iv["ours"][0] <= iv["tribes"][1] and iv["tribes"][0] <= iv["ours"][1]
        ok &= c2 and c3
        print(f"| {a} v {b} | {cells[0]} | {cells[1]} | {'pass' if c2 else 'FAIL'} | {'pass' if c3 else 'FAIL'} |")
    for a in AGENTS[:-1]:
        lo = wilson(*data["ours"][(a, "Random")])[0]
        ok &= lo >= 0.95
        print(f"§6.1 {a} v Random lower bound {lo:.1%}: {'pass' if lo >= 0.95 else 'FAIL'}")
    print("ADR-007 §6: ALL PASS" if ok else "ADR-007 §6: SOME FAIL")

    fig, ax = plt.subplots(figsize=(8, 0.42 * len(pairs) + 1.6), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)
    for i, (a, b) in enumerate(pairs):
        for j, (side, label, color) in enumerate(SIDES):
            w, n = data[side][(a, b)]
            lo, hi = wilson(w, n)
            y = i + (j - 0.5) * 0.32
            ax.plot([lo, hi], [y, y], color=color, linewidth=2, solid_capstyle="round", clip_on=False)
            ax.plot(w / n, y, "o", color=color, markersize=6, markeredgecolor=SURFACE, markeredgewidth=1.5,
                    label=label if i == 0 else None, clip_on=False)
    ax.axvline(0.5, color=INK2, linewidth=1, linestyle=(0, (3, 3)))
    ax.set_yticks(range(len(pairs)), [f"{a} v {b}" for a, b in pairs], fontsize=8, color=INK)
    ax.invert_yaxis()
    ax.set_xlim(0, 1)
    ax.xaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
    ax.tick_params(axis="x", labelsize=8, colors=INK2)
    ax.tick_params(axis="y", length=0)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_xlabel("first agent's win rate vs. second (dot) with Wilson 95% interval", fontsize=8, color=INK2)
    ax.set_title("Search baselines, paper condition: our reimplementations vs. Tribes' agents",
                 fontsize=10, color=INK, loc="left", pad=26)
    ax.legend(loc="lower left", fontsize=8, frameon=False, bbox_to_anchor=(0, 1.0), ncol=2, borderaxespad=0.2)
    fig.tight_layout()
    fig.savefig(out, facecolor=SURFACE)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
