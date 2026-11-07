#!/usr/bin/env python3
"""Generates difftest logs for many seeds: 2..8 players, both modes, tribe sets rotated so every tribe
appears in every player count and capital order.

Default: map-load logs (OracleLog --init-only), all Random agents. With --full: whole games with
alternating Random / Rule Based agents (full logs are large: ~8 MB for 2 players).

--scenario=mindbender (with --full): every tribe starts with Philosophy; 2..4 players, our MindBender agent
alternating with Random, level seeds from 200000 (OracleLog / MindBenderAgent.java).

--partial: use the partial-obs oracle build (build/tribes-po, ADR-004) instead of stock Tribes.

--start=K: seeds K .. K+N-1 instead of 0 .. N-1 (same games as one big corpus; used for batches).

Usage: gen_init_corpus.py N OUT_DIR [--full] [--scenario=mindbender] [--partial] [--start=K]   (repo root,
after `make setup`)
"""

import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

TRIBES = ["Xin Xi", "Imperius", "Bardur", "Oumaji", "Kickoo", "Hoodrick",
          "Luxidoor", "Vengir", "Zebasi", "Ai-Mo", "Quetzali", "Yadakk"]


def job(k: int, out_dir: str, full: bool, extra: list[str], cp: str) -> None:
    scenario = bool(extra)
    players = 2 + k % (3 if scenario else 7)
    tribes = [TRIBES[(k + 5 * i) % 12] for i in range(players)]  # 5 is coprime to 12: distinct
    mode = "Capitals" if k % 2 == 0 else "Score"
    level_seed = (200_000 if scenario else 100_000) + k
    seats = ["MindBender", "Random"] if scenario else ["Random", "Rule Based"]
    agents = [seats[(k + i) % 2] if full else "Random" for i in range(players)]
    args = [str(level_seed), str(k), str(k), mode, ",".join(agents), ",".join(tribes)]
    if not full:
        args.append("--init-only")
    args += extra
    path = os.path.join(out_dir, f"{'game' if full else 'init'}-{level_seed}.log")
    with open(path, "w", encoding="utf-8") as f:
        subprocess.run(["java", "-cp", cp, "OracleLog", *args], cwd="oracle/tribes", stdout=f,
                       stderr=subprocess.PIPE, check=True, timeout=600 if full else 120)


def main() -> int:
    n, out_dir = int(sys.argv[1]), sys.argv[2]
    full = "--full" in sys.argv[3:]
    extra = [a for a in sys.argv[3:] if a.startswith("--scenario=")]
    build = "tribes-po" if "--partial" in sys.argv[3:] else "tribes"
    cp = f"../../build/{build}:lib/json.jar"
    start = next((int(a.split("=", 1)[1]) for a in sys.argv[3:] if a.startswith("--start=")), 0)
    os.makedirs(out_dir, exist_ok=True)
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        list(ex.map(lambda k: job(k, out_dir, full, extra, cp), range(start, start + n)))
    print(f"{n} logs in {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
