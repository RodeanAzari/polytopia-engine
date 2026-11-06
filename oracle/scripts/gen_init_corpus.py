#!/usr/bin/env python3
"""Generates map-load difftest logs (OracleLog --init-only) for many seeds: 2..8 players, both modes,
tribe sets rotated so every tribe appears in every player count and capital order.

Usage: gen_init_corpus.py N OUT_DIR   (run from the repo root after `make setup`)
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

TRIBES = ["Xin Xi", "Imperius", "Bardur", "Oumaji", "Kickoo", "Hoodrick",
          "Luxidoor", "Vengir", "Zebasi", "Ai-Mo", "Quetzali", "Yadakk"]
CP = "../../build/tribes:lib/json.jar"


def job(k: int, out_dir: str) -> None:
    players = 2 + k % 7
    tribes = [TRIBES[(k + 5 * i) % 12] for i in range(players)]  # 5 is coprime to 12: distinct
    mode = "Capitals" if k % 2 == 0 else "Score"
    level_seed = 100_000 + k
    args = [str(level_seed), str(k), str(k), mode, ",".join(["Random"] * players), ",".join(tribes),
            "--init-only"]
    path = os.path.join(out_dir, f"init-{level_seed}.log")
    with open(path, "w", encoding="utf-8") as f:
        subprocess.run(["java", "-cp", CP, "OracleLog", *args], cwd="oracle/tribes", stdout=f,
                       stderr=subprocess.PIPE, check=True, timeout=120)


def main() -> int:
    n, out_dir = int(sys.argv[1]), sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        list(ex.map(lambda k: job(k, out_dir), range(n)))
    print(f"{n} logs in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
