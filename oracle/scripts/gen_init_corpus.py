#!/usr/bin/env python3
"""Generates difftest logs for many seeds: 2..8 players, both modes, tribe sets rotated so every tribe
appears in every player count and capital order.

Default: map-load logs (OracleLog --init-only), all Random agents. With --full: whole games with
alternating Random / Rule Based agents (full logs are large: ~8 MB for 2 players).

Usage: gen_init_corpus.py N OUT_DIR [--full]   (run from the repo root after `make setup`)
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

TRIBES = ["Xin Xi", "Imperius", "Bardur", "Oumaji", "Kickoo", "Hoodrick",
          "Luxidoor", "Vengir", "Zebasi", "Ai-Mo", "Quetzali", "Yadakk"]
CP = "../../build/tribes:lib/json.jar"


def job(k: int, out_dir: str, full: bool) -> None:
    players = 2 + k % 7
    tribes = [TRIBES[(k + 5 * i) % 12] for i in range(players)]  # 5 is coprime to 12: distinct
    mode = "Capitals" if k % 2 == 0 else "Score"
    level_seed = 100_000 + k
    agents = [["Random", "Rule Based"][(k + i) % 2] if full else "Random" for i in range(players)]
    args = [str(level_seed), str(k), str(k), mode, ",".join(agents), ",".join(tribes)]
    if not full:
        args.append("--init-only")
    path = os.path.join(out_dir, f"{'game' if full else 'init'}-{level_seed}.log")
    with open(path, "w", encoding="utf-8") as f:
        subprocess.run(["java", "-cp", CP, "OracleLog", *args], cwd="oracle/tribes", stdout=f,
                       stderr=subprocess.PIPE, check=True, timeout=600 if full else 120)


def main() -> int:
    n, out_dir = int(sys.argv[1]), sys.argv[2]
    full = "--full" in sys.argv[3:]
    os.makedirs(out_dir, exist_ok=True)
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        list(ex.map(lambda k: job(k, out_dir, full), range(n)))
    print(f"{n} logs in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
