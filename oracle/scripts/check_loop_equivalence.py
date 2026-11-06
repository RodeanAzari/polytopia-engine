#!/usr/bin/env python3
"""Acceptance check for the oracle logging bridge: our driver (OracleLog) must reproduce stock
Tribes' Game.run on the same seeds: same final tick, and the same result and score per tribe type.
Only reproducible agents (Random, Rule Based) can be compared; search agents use unseeded RNGs.
Also checks that OracleLog output is byte-identical across two runs.

Usage: check_loop_equivalence.py N   (run from the repo root after `make setup`)
"""
import os
import re
import subprocess
import sys

CP = "../../build/tribes:lib/json.jar"
CONFIGS = [  # (mode, players, tribes)
    ("Capitals", "Random,Rule Based", "Xin Xi,Imperius"),
    ("Score", "Rule Based,Random", "Bardur,Oumaji"),
    ("Capitals", "Random,Random,Rule Based", "Kickoo,Hoodrick,Luxidoor"),
    ("Score", "Rule Based,Rule Based,Random,Random", "Vengir,Zebasi,Ai-Mo,Quetzali"),
]


TIMEOUT_S = int(os.environ.get("ORACLE_TIMEOUT_S", "120")) # a stock game takes a few seconds; a hang is a failure, not a wait


class GameFailed(Exception):
    pass


def java(main: str, args: list[str]) -> str:
    try:
        r = subprocess.run(["java", "-cp", CP, main, *args], cwd="oracle/tribes",
                           capture_output=True, text=True, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired as e:
        raise GameFailed(f"{main} timed out after {TIMEOUT_S}s") from e
    if r.returncode != 0:
        raise GameFailed(f"{main} exited {r.returncode}: {r.stderr.strip()[-300:]}")
    return r.stdout


def stock(args: list[str]) -> tuple[int, dict[str, tuple[str, int]]]:
    out = java("OracleRun", args)
    tick = int(re.search(r"^(\d+); Game Results:", out, re.M).group(1))
    res = {t: (r, int(s)) for t, r, s in re.findall(r"Tribe (\w+) \([^)]*\): (\w+), (-?\d+) points", out)}
    return tick, res


def ours(args: list[str]) -> tuple[int, dict[str, tuple[str, int]], str]:
    out = java("OracleLog", [*args, "--no-states"])
    tick = int(re.search(r"^GAMEEND (\d+)$", out, re.M).group(1))
    types = dict(re.findall(r"^SEAT (\d+) (\w+) \d+$", out, re.M))
    res = {types[i]: (r, int(s)) for i, r, s in re.findall(r"^RESULT (\d+) (\w+) (-?\d+)$", out, re.M)}
    return tick, res, out


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    games = fails = 0
    for mode, players, tribes in CONFIGS:
        for seed in range(1, n + 1):
            args = [str(1000 + seed), str(seed), str(seed), mode, players, tribes]
            games += 1
            try:
                s = stock(args)
                o = ours(args)
            except GameFailed as e:
                fails += 1
                print(f"FAILED {args}: {e}")
                continue
            if s != o[:2]:
                fails += 1
                print(f"MISMATCH {args}: stock={s} ours={o[:2]}")
            if seed == 1 and java("OracleLog", args) != java("OracleLog", args):
                fails += 1
                print(f"NOT REPRODUCIBLE (full log) {args}")
    print(f"{games} games, {fails} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
