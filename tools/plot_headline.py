#!/usr/bin/env python3
"""Phase 3 headline chart and table (docs/buildplan.md §11, ADR-007 §3): our PUCT's win rate against each
baseline with its Wilson 95% interval, in the Paper (full obs) and Fog (partial obs) conditions, and the flat
variant (PUCT:macro=0) next to it where it was played (the macro vs. flat comparison). Writes <out>.png and
prints the markdown table, including the "beats every baseline" check (each interval's lower bound > 50%).

Usage: plot_headline.py <out.png> <csv>...   (tools/headline.sh CSVs; condition from the obs column)
"""

from __future__ import annotations

import csv
import math
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
OPPONENTS = ["RHEA", "RuleBased", "MCTS", "OSLA", "MC", "Random"]
SHOWN = {"RuleBased": "Rule Based"}
CONDITIONS = [("full", "Paper (full observability)"), ("partial", "Fog (partial observability)")]
VARIANTS = [("PUCT", "PUCT (macro, default)", "#2a78d6"), ("PUCT:macro=0", "PUCT flat (macro=0)", "#eb6834")]


def wilson(w: float, n: int, z: float = 1.96) -> tuple[float, float]:
    p = w / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return (c - h) / d, (c + h) / d


Cell = tuple[float, int, int]  # (wins + draws/2, games, draws) of the PUCT variant


def tally(paths: list[str]) -> dict[tuple[str, str, str], Cell]:
    """(obs, variant, opponent) -> the variant's score against that opponent, from either seat."""
    t: dict[tuple[str, str, str], Cell] = {}
    for path in paths:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                seats = (r["agent0"], r["agent1"])
                mine = 0 if seats[0].startswith("PUCT") else 1
                variant, opp = seats[mine], seats[1 - mine]
                s0 = {"WIN": 1.0, "DRAW": 0.5}.get(r["result0"], 0.0)
                s = s0 if mine == 0 else 1.0 - s0
                w, n, d = t.get((r["obs"], variant, opp), (0.0, 0, 0))
                t[(r["obs"], variant, opp)] = (w + s, n + 1, d + (r["result0"] == "DRAW"))
    return t


def table(t: dict[tuple[str, str, str], Cell]) -> bool:
    """Prints the markdown table; True if the default PUCT's every interval clears 50% in every condition."""
    print("| opponent | " + " | ".join(f"{v} · {c}" for c, _ in CONDITIONS for v, _, _ in VARIANTS) + " |")
    print("|---|" + "---|" * (len(CONDITIONS) * len(VARIANTS)))
    ok = True
    for opp in OPPONENTS:
        cells = []
        for obs, _ in CONDITIONS:
            for v, _, _ in VARIANTS:
                if (obs, v, opp) not in t:
                    cells.append("—")
                    ok &= v != "PUCT"  # a default cell not played can't count as beaten
                    continue
                w, n, d = t[(obs, v, opp)]
                lo, hi = wilson(w, n)
                cells.append(f"{w / n:.1%} ({n}; {lo:.1%}–{hi:.1%}){f'; {d} draws' if d else ''}")
                if v == "PUCT":
                    ok &= lo > 0.5
        print(f"| {SHOWN.get(opp, opp)} | " + " | ".join(cells) + " |")
    return ok


def plot(t: dict[tuple[str, str, str], Cell], out: str) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), dpi=150, sharey=True)
    fig.patch.set_facecolor(SURFACE)
    for ax, (obs, title) in zip(axes, CONDITIONS, strict=True):
        ax.set_facecolor(SURFACE)
        for i, opp in enumerate(OPPONENTS):
            for j, (v, label, color) in enumerate(VARIANTS):
                if (obs, v, opp) not in t:
                    continue
                w, n, _ = t[(obs, v, opp)]
                lo, hi = wilson(w, n)
                y = i + (j - 0.5) * 0.34 if (obs, "PUCT:macro=0", opp) in t else i
                ax.plot([lo, hi], [y, y], color=color, linewidth=2, solid_capstyle="round", clip_on=False)
                ax.plot(w / n, y, "o", color=color, markersize=7, markeredgecolor=SURFACE, markeredgewidth=1.5,
                        label=label if i == 0 else None, clip_on=False)
        ax.axvline(0.5, color=INK2, linewidth=1, linestyle=(0, (3, 3)))
        ax.set_yticks(range(len(OPPONENTS)), [f"vs. {SHOWN.get(o, o)}" for o in OPPONENTS], fontsize=8,
                      color=INK)
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
        ax.tick_params(axis="x", labelsize=8, colors=INK2)
        ax.tick_params(axis="y", length=0)
        ax.grid(axis="x", color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
        for s in ax.spines.values():
            s.set_visible(False)
        ax.set_title(title, fontsize=9, color=INK, loc="left")
        ax.set_xlabel("PUCT's win rate (dot) with Wilson 95% interval", fontsize=8, color=INK2)
    axes[0].set_ylim(len(OPPONENTS) - 0.4, -0.6)  # top row first, half a row of room at both ends
    handles, labels = axes[0].get_legend_handles_labels()
    fig.suptitle("Phase 3: action-level PUCT vs. the Tribes baselines, 100 test levels, 2,000 FM calls per move",
                 fontsize=10, color=INK, x=0.01, y=0.98, ha="left")
    fig.legend(handles, labels, loc="upper left", fontsize=8, frameon=False, ncol=2, bbox_to_anchor=(0.005, 0.95))
    fig.tight_layout(rect=(0, 0, 1, 0.9), w_pad=3.0)
    fig.savefig(out, facecolor=SURFACE)


def main() -> int:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    t = tally(sys.argv[2:])
    ok = table(t)
    plot(t, sys.argv[1])
    print("Beats every baseline in both conditions (each 95% lower bound > 50%, no cell missing):",
          "YES" if ok else "NO")
    return 0


if __name__ == "__main__":
    sys.exit(main())
