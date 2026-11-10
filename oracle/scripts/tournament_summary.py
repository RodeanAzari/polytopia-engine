#!/usr/bin/env python3
"""Summary of a tools/tournament.cpp CSV (ADR-007 §3, §7): per agent and per pair W/D/L, win rate
(W + D/2) / N with a Wilson 95% interval, written next to the CSV as .md and printed.

Usage: tournament_summary.py CSV [--hardware="..."] [--command="..."]   (hardware: detected by default)   (repo root)
"""

import csv
import os
import sys
from pathlib import Path

from tribes_tournament import wilson


def opt(name: str, default: str) -> str:
    return next((a.split("=", 1)[1] for a in sys.argv[2:] if a.startswith(f"--{name}=")), default)


def hardware() -> str:
    """CPU model and thread count of this machine (CLAUDE.md §10)."""
    model = "unknown CPU"
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        model = next((line.split(":", 1)[1].strip() for line in cpuinfo.read_text().splitlines()
                      if line.startswith("model name")), model)
    return f"{model}, {os.cpu_count()} threads"


def tally(rows: list[dict]) -> tuple[list[str], dict, dict]:
    agents: list[str] = []
    per: dict = {}   # agent -> [W, D, L]
    pair: dict = {}  # (a, b) -> [W, D, L] of a against b
    for r in rows:
        for i, j in ((0, 1), (1, 0)):
            a, b = r[f"agent{i}"], r[f"agent{j}"]
            if a not in agents:
                agents.append(a)
            res = r["result0"] if i == 0 else {"WIN": "LOSS", "LOSS": "WIN", "DRAW": "DRAW"}[r["result0"]]
            k = {"WIN": 0, "DRAW": 1, "LOSS": 2}[res]
            per.setdefault(a, [0, 0, 0])[k] += 1
            pair.setdefault((a, b), [0, 0, 0])[k] += 1
    return agents, per, pair


def rate(wdl: list[int]) -> tuple[float, float, float, int]:
    n = sum(wdl)
    points = wdl[0] + wdl[1] / 2
    lo, hi = wilson(points, n)
    return points / n, lo, hi, n


def main() -> int:
    path = Path(sys.argv[1])
    with path.open(encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    agents, per, pair = tally(rows)
    first = rows[0]
    lines = [f"# Tournament {first['tournament']}", "",
             (f"Our simulator, {len(rows)} games, obs={first['obs']}, levels: "
              f"{len({r['levelSeed'] for r in rows})} seeds, seat-swapped pairs (ADR-007). "
              "Win rate = (W + D/2) / N, Wilson 95%."),
             f"Hardware: {opt('hardware', hardware())}. Command: `{opt('command', '[TBD]')}`.",
             f"Illegal returned actions (no-ops): {sum(int(r['illegal']) for r in rows)}.", "",
             "| agent | games | W | D | L | win rate | 95% CI |", "|---|---|---|---|---|---|---|"]
    for a in sorted(agents, key=lambda a: -rate(per[a])[0]):
        p, lo, hi, n = rate(per[a])
        w, d, losses = per[a]
        lines.append(f"| {a} | {n} | {w} | {d} | {losses} | {p:.1%} | {lo:.1%}–{hi:.1%} |")
    lines += ["", "Row agent against column agent: win rate (games; 95% CI):", "",
              "| | " + " | ".join(agents) + " |", "|---" * (len(agents) + 1) + "|"]
    for a in agents:
        cells = []
        for b in agents:
            if (a, b) not in pair:
                cells.append("—")
                continue
            p, lo, hi, n = rate(pair[(a, b)])
            cells.append(f"{p:.1%} ({n}; {lo:.1%}–{hi:.1%})")
        lines.append(f"| {a} | " + " | ".join(cells) + " |")
    text = "\n".join(lines) + "\n"
    path.with_suffix(".md").write_text(text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
