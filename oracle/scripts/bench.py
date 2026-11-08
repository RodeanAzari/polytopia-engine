#!/usr/bin/env python3
"""Simulator benchmark (`make bench`): Tribes' Java forward model vs. our C++ simulator on uniform-random
playouts of the same generated levels (OracleBench.java writes them, tools/bench.cpp replays them).

Configs run one at a time (never concurrently), each side single-threaded. Per-game rows go to
data/results/bench-<stamp>.csv; a summary table (decisions/s, playouts/s, speedup) is printed and
written next to it as bench-<stamp>.md together with the hardware and git hash.

Usage: bench.py N [--warmup=W] [--players=2,4,8] [--modes=Capitals,Score]   (repo root, after `make setup build`)
"""

import csv
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

COLS = ["side", "players", "mode", "levelSeed", "ticks", "steps", "turns", "seconds"]


def opt(name: str, default: str) -> str:
    return next((a.split("=", 1)[1] for a in sys.argv[2:] if a.startswith(f"--{name}=")), default)


def hardware() -> str:
    cpu = next((ln.split(":", 1)[1].strip() for ln in Path("/proc/cpuinfo").read_text().splitlines()
                if ln.startswith("model name")), platform.processor())
    java = subprocess.run(["java", "-version"], capture_output=True, text=True, check=True).stderr.splitlines()[0]
    wsl = " (WSL2)" if "microsoft" in platform.release().lower() else ""
    return f"{cpu}, {os.cpu_count()} threads, {platform.system()} {platform.release()}{wsl}; {java}"


def rows(text: str) -> list[dict]:
    return [dict(zip(COLS, ln.split(","))) for ln in text.splitlines() if ln.strip()]


def main() -> int:
    n = int(sys.argv[1])
    warmup = int(opt("warmup", "50"))
    players = [int(p) for p in opt("players", "2,4,8").split(",")]
    modes = opt("modes", "Capitals,Score").split(",")
    stamp = time.strftime("%Y%m%d-%H%M")
    out_csv = Path(f"data/results/bench-{stamp}.csv")
    level_root = Path("build/bench/levels")
    all_rows: list[dict] = []
    for mode in modes:
        for p in players:
            level_dir = level_root / f"{p}-{mode}"
            first = 10_000 * p + (0 if mode == "Capitals" else 5_000)
            java = subprocess.run(
                ["java", "-cp", "../../build/tribes:lib/json.jar", "core.game.OracleBench", str(p), mode,
                 str(first), str(n), str(warmup), str(level_dir.resolve())],
                cwd="oracle/tribes", capture_output=True, text=True, check=True)
            levels = [str(level_dir / f"level-{p}-{mode}-{first + k}.txt") for k in range(n)]
            sim = subprocess.run(["build/release/bench", *levels], capture_output=True, text=True, check=True)
            all_rows += rows(java.stdout) + rows(sim.stdout)
            print(f"  {p} players {mode}: done", file=sys.stderr)

    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLS)
        w.writeheader()
        w.writerows(all_rows)

    git = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                         check=True).stdout.strip()
    dirty = "-dirty" if subprocess.run(["git", "diff", "--quiet", "HEAD"], check=False).returncode else ""
    lines = [f"# Simulator benchmark {stamp}", "",
             f"- git {git}{dirty}; {n} timed games per side and config ({warmup} Java warm-up games, untimed)",
             f"- hardware: {hardware()}",
             "- workload: uniform-random playouts on the same Tribes-generated levels, full obs; action generation",
             "  after every executed action; no state copies; level load not timed; one game thread per side",
             f"- per-game rows: {out_csv}", "",
             ("| players | mode | Tribes decisions/s | ours decisions/s | speedup | Tribes playouts/s | ours playouts/s "
              "| decisions/game (Tribes / ours) | per-game speedup, median |"),
             "|---|---|---|---|---|---|---|---|---|"]
    for mode in modes:
        for p in players:
            sel = {s: [r for r in all_rows if r["side"] == s and r["players"] == str(p) and r["mode"] == mode]
                   for s in ("tribes", "sim")}
            secs = {s: sum(float(r["seconds"]) for r in v) for s, v in sel.items()}
            steps = {s: sum(int(r["steps"]) for r in v) for s, v in sel.items()}
            rate = {s: steps[s] / secs[s] for s in sel}
            games = {s: len(v) / secs[s] for s, v in sel.items()}
            per_game = {s: [int(r["steps"]) / float(r["seconds"]) for r in v] for s, v in sel.items()}
            med = statistics.median(per_game["sim"]) / statistics.median(per_game["tribes"])
            lines.append(f"| {p} | {mode} | {rate['tribes']:,.0f} | {rate['sim']:,.0f} | {rate['sim'] / rate['tribes']:.1f}x "
                         f"| {games['tribes']:.1f} | {games['sim']:.1f} "
                         f"| {steps['tribes'] / n:,.0f} / {steps['sim'] / n:,.0f} | {med:.1f}x |")
    report = "\n".join(lines) + "\n"
    out_csv.with_suffix(".md").write_text(report)
    print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
