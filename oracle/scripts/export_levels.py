#!/usr/bin/env python3
"""Exports Tribes-generated levels as files our tools load (tools/random_game.hpp, load_level_file):
OUT_DIR/level-<players>-<mode>-<seed>.txt, first line the mode, then the level lines.

Each level comes from `OracleLog --init-only` (the same generator call as the Tribes games and the
difftests), so a level file and a Tribes game on the same seed and tribes start from the same map.
Levels are Tribes output: regenerate them, don't commit them (ADR-002).

Usage: export_levels.py SEEDS_FILE OUT_DIR [--tribes=Xin Xi,Imperius] [--mode=Capitals]   (repo root)
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def opt(name: str, default: str) -> str:
    return next((a.split("=", 1)[1] for a in sys.argv[3:] if a.startswith(f"--{name}=")), default)


def export(seed: str, out_dir: str, tribes: str, mode: str) -> None:
    players = len(tribes.split(","))
    agents = ",".join(["Random"] * players)
    r = subprocess.run(["java", "-cp", "../../build/tribes:lib/json.jar", "OracleLog", seed, "0", "0", mode,
                        agents, tribes, "--init-only"], cwd="oracle/tribes", capture_output=True, text=True,
                       check=True, timeout=120)
    lines = r.stdout.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("LEVEL "))
    n = int(lines[start].split()[1])
    with open(os.path.join(out_dir, f"level-{players}-{mode}-{seed}.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join([mode, *lines[start + 1:start + 1 + n]]) + "\n")


def main() -> int:
    seeds = Path(sys.argv[1]).read_text(encoding="utf-8").split()
    out_dir = sys.argv[2]
    tribes, mode = opt("tribes", "Xin Xi,Imperius"), opt("mode", "Capitals")
    os.makedirs(out_dir, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as ex:
        list(ex.map(lambda s: export(s, out_dir, tribes, mode), seeds))
    print(f"{len(seeds)} levels in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
