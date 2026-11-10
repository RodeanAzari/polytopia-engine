#!/usr/bin/env python3
"""Replay difftest at scale without keeping a corpus: generates oracle games in batches (same seeds as one
gen_init_corpus.py run), replays each batch with oracle_diff, sums the results and deletes the logs.
Failing logs are kept in OUT_DIR/failed/ for debugging. Logs are ~40 MB per game, so 1,000 games would not
fit as one corpus.

A replay that stops at an unimplemented action type counts as a failure here (every type is implemented).

Usage: difftest_batched.py N OUT_DIR [--batch=B] [--start=K] [--scenario=...] [--partial] [--probe=P]
(repo root, after `make setup` and `make build`). Exit code 0 iff every game matched.
"""

import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

SUMMARY = re.compile(r"(\d+) logs: (\d+) replayed to the end, (\d+) failures, (\d+) states, (\d+) "
                     r"legal-action sets and (\d+) observations matched")
FAILED_LOG = re.compile(r"^(?:MISMATCH|ERROR) (\S+?):?(?: |$)", re.M)
PROBES = re.compile(r"agent-side forward model \(04 §9\): (\d+) probes, (\d+) search states and (\d+) legal-action")


def flag(name: str, default: int) -> int:
    return next((int(a.split("=", 1)[1]) for a in sys.argv[3:] if a.startswith(f"--{name}=")), default)


def main() -> int:
    n, out = int(sys.argv[1]), Path(sys.argv[2])
    batch, start = flag("batch", 32), flag("start", 0)
    passthrough = [a for a in sys.argv[3:]
                   if a.startswith(("--scenario=", "--probe=")) or a == "--partial"]
    work, failed = out / "batch", out / "failed"
    shutil.rmtree(out, ignore_errors=True)
    failed.mkdir(parents=True)
    totals = {"games": 0, "complete": 0, "failures": 0, "states": 0, "legal": 0, "obs": 0,
              "probes": 0, "pstates": 0, "plegal": 0}
    t0 = time.time()
    for first in range(start, start + n, batch):
        size = min(batch, start + n - first)
        shutil.rmtree(work, ignore_errors=True)
        subprocess.run([sys.executable, "oracle/scripts/gen_init_corpus.py", str(size), str(work), "--full",
                        f"--start={first}", *passthrough], check=True, stdout=subprocess.DEVNULL)
        r = subprocess.run(["build/release/oracle_diff", *sorted(map(str, work.glob("*.log")))],
                           capture_output=True, text=True, check=False)
        m = SUMMARY.search(r.stdout)
        if m is None:
            print(r.stdout + r.stderr, file=sys.stderr)
            raise RuntimeError(f"oracle_diff gave no summary for seeds {first}..{first + size - 1}")
        games, complete, failures, states, legal, obs = map(int, m.groups())
        p = PROBES.search(r.stdout)
        if p is not None:
            for name, v in zip(("probes", "pstates", "plegal"), map(int, p.groups())):
                totals[name] += v
        stopped = games - complete - failures  # stopped at an unimplemented action type
        for name, v in (("games", games), ("complete", complete), ("states", states), ("legal", legal),
                        ("obs", obs)):
            totals[name] += v
        totals["failures"] += failures + stopped
        for path in FAILED_LOG.findall(r.stderr):
            shutil.copy(path, failed)
        if r.stderr:
            sys.stderr.write(r.stderr)
        if stopped:
            sys.stderr.write(r.stdout)
        print(f"seeds {first}..{first + size - 1}: {complete}/{games} matched; total "
              f"{totals['complete']}/{totals['games']}, {totals['states']:,} states, "
              f"{totals['legal']:,} legal sets, {totals['obs']:,} observations, {totals['failures']} failures, "
              f"{time.time() - t0:.0f} s",
              flush=True)
    shutil.rmtree(work, ignore_errors=True)
    print(f"{totals['games']} games: {totals['complete']} replayed to the end, {totals['failures']} failures, "
          f"{totals['states']:,} states, {totals['legal']:,} legal-action sets and {totals['obs']:,} observations "
          "matched")
    if totals["probes"]:
        print(f"agent-side forward model (04 §9): {totals['probes']:,} probes, {totals['pstates']:,} search "
              f"states and {totals['plegal']:,} legal-action sets matched")
    return 0 if totals["failures"] == 0 and totals["complete"] == totals["games"] else 1


if __name__ == "__main__":
    sys.exit(main())
