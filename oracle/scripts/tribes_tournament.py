#!/usr/bin/env python3
"""Round-robin tournament of Tribes' own agents in Tribes (the Phase 2 reference numbers), every seed fixed.

Every pair of agents plays 2-player games on each level seed in both seatings (each agent plays each tribe on
each map). Level seeds default to the 25 of the stock Tribes tournament.json; tribes default to its Xin Xi vs.
Imperius. One JVM per game (oracle/bridge/OracleMatch.java), at most --jobs at once, each capped at --xmx.
Results are appended per game to the CSV, so an interrupted run resumes where it stopped (rerun the same command
with --out pointing at the same file). The summary (per-agent and per-pair win rates with Wilson 95% intervals)
is printed and written next to the CSV as .md.

Tribes' search agents draw from an unseeded Random inside GameState.copy(): reruns agree only statistically.

Usage: tribes_tournament.py [--agents=Random,Rule Based,OSLA,MC,MCTS,RHEA] [--seeds=K] [--mode=Capitals]
                            [--jobs=8] [--xmx=1g] [--out=data/results/tribes-rr-<stamp>.csv] [--summary-only]
"""

import csv
import itertools
import json
import math
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

COLS = ["game", "levelSeed", "seat0", "seat1", "tribe0", "tribe1", "result0", "result1", "score0", "score1",
        "seconds"]
TRIBES = ["Xin Xi", "Imperius"]


def opt(name: str, default: str) -> str:
    return next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith(f"--{name}=")), default)


def wilson(wins: int, n: int, z: float = 1.959964) -> tuple[float, float]:
    """Wilson score interval for a binomial proportion (95% by default)."""
    if n == 0:
        return 0.0, 1.0
    p = wins / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return max(0.0, centre - half), min(1.0, centre + half)


def schedule(agents: list[str], seeds: list[str]) -> list[dict]:
    games = []
    for a, b in itertools.combinations(agents, 2):
        for seed in seeds:
            for seat0, seat1 in ((a, b), (b, a)):
                games.append({"game": len(games), "levelSeed": seed, "seat0": seat0, "seat1": seat1})
    return games


def play(g: dict, mode: str, xmx: str) -> dict:
    cmd = ["java", f"-Xmx{xmx}", "-cp", "../../build/tribes:lib/json.jar", "OracleMatch", g["levelSeed"],
           str(g["game"]), str(1000 * g["game"]), mode, f"{g['seat0']},{g['seat1']}", ",".join(TRIBES)]
    out = subprocess.run(cmd, cwd="oracle/tribes", capture_output=True, text=True, check=True).stdout
    res = [ln.split() for ln in out.splitlines() if ln.startswith("RESULT ")]
    if len(res) != 2:
        raise RuntimeError(f"game {g['game']}: unexpected output {out!r}")
    return {**g, "tribe0": res[0][3], "tribe1": res[1][3], "result0": res[0][4], "result1": res[1][4],
            "score0": res[0][5], "score1": res[1][5], "seconds": res[0][9]}


def summary(rows: list[dict], agents: list[str], header: list[str]) -> str:
    wins = {a: 0 for a in agents}
    played = {a: 0 for a in agents}
    pair = {}  # (a, b) -> [wins of a, games]
    for r in rows:
        for i, j in ((0, 1), (1, 0)):
            a, b = r[f"seat{i}"], r[f"seat{j}"]
            w = r[f"result{i}"] == "WIN"
            wins[a] += w
            played[a] += 1
            p = pair.setdefault((a, b), [0, 0])
            p[0] += w
            p[1] += 1
    lines = header + ["", "| agent | games | wins | win rate | 95% CI (Wilson) |", "|---|---|---|---|---|"]
    for a in sorted(agents, key=lambda a: -(wins[a] / played[a] if played[a] else 0)):
        lo, hi = wilson(wins[a], played[a])
        rate = wins[a] / played[a] if played[a] else float("nan")
        lines.append(f"| {a} | {played[a]} | {wins[a]} | {rate:.1%} | {lo:.1%}–{hi:.1%} |")
    lines += ["", "Row agent's win rate against column agent (games; 95% CI):", "",
              "| | " + " | ".join(agents) + " |", "|---" * (len(agents) + 1) + "|"]
    for a in agents:
        cells = []
        for b in agents:
            if a == b or (a, b) not in pair:
                cells.append("—")
                continue
            w, n = pair[(a, b)]
            lo, hi = wilson(w, n)
            cells.append(f"{w / n:.0%} ({n}; {lo:.0%}–{hi:.0%})")
        lines.append(f"| {a} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def main() -> int:
    agents = opt("agents", "Random,Rule Based,OSLA,MC,MCTS,RHEA").split(",")
    seeds = json.loads(Path("oracle/tribes/tournament.json").read_text())["Level Seeds"]
    seeds = seeds[: int(opt("seeds", str(len(seeds))))]
    mode, jobs, xmx = opt("mode", "Capitals"), int(opt("jobs", "8")), opt("xmx", "1g")
    out = Path(opt("out", f"data/results/tribes-rr-{time.strftime('%Y%m%d-%H%M')}.csv"))
    games = schedule(agents, seeds)

    done: dict[int, dict] = {}
    if out.exists():
        with out.open(newline="") as f:
            done = {int(r["game"]): r for r in csv.DictReader(f)}
    todo = [g for g in games if g["game"] not in done]
    if "--summary-only" not in sys.argv[1:] and todo:
        out.parent.mkdir(parents=True, exist_ok=True)
        new_file = not out.exists()
        lock = threading.Lock()
        with out.open("a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=COLS)
            if new_file:
                w.writeheader()
            t0 = time.time()

            def run(g: dict) -> None:
                r = play(g, mode, xmx)
                with lock:
                    w.writerow(r)
                    f.flush()
                    done[g["game"]] = r
                    print(f"  {len(done)}/{len(games)} games, {time.time() - t0:.0f} s", file=sys.stderr)

            with ThreadPoolExecutor(max_workers=jobs) as ex:
                list(ex.map(run, todo))

    rows = [done[g["game"]] for g in games if g["game"] in done]
    git = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                         check=True).stdout.strip()
    cpu = next((ln.split(":", 1)[1].strip() for ln in Path("/proc/cpuinfo").read_text().splitlines()
                if ln.startswith("model name")), "?")
    header = [f"# Tribes round-robin ({out.stem})", "",
              (f"- {len(rows)}/{len(games)} games; 2 players ({' vs. '.join(TRIBES)}), {mode} mode, full obs, both "
               f"seatings per level seed; {len(seeds)} level seeds from the stock tournament.json"),
              ("- agents: Tribes' own at the stock tournament.json parameters (2,000 forward-model calls per "
               "decision where budgeted); agent i seeded agentSeed + i"),
              (f"- git {git}, Tribes 270e7e5; {cpu}, {os.cpu_count()} threads; {jobs} games in parallel, "
               f"-Xmx{xmx}"),
              f"- per-game rows: {out}"]
    report = summary(rows, agents, header)
    out.with_suffix(".md").write_text(report)
    print(report)
    return 0 if len(rows) == len(games) else 1


if __name__ == "__main__":
    sys.exit(main())
