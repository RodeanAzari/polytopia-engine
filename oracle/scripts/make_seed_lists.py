#!/usr/bin/env python3
"""Generates the ADR-007 §2 level-seed lists data/seeds/dev.txt and test.txt from a recorded master seed:
100 seeds each, disjoint from each other and from paper-25.txt; then train.txt (ADR-012 §2: 2,000 seeds
for self-play, disjoint from all three), drawn after dev and test so those two are unchanged. Run once;
--check verifies the committed files still match the generator (deterministic: Python's Mersenne Twister, seeded by an int, is stable).

Usage (repo root): make_seed_lists.py [--check]
"""

import random
import sys
from pathlib import Path

MASTER_SEED = 20261110  # recorded in ADR-007 §2; never change (the lists would change)
COUNTS = {"dev": 100, "test": 100, "train": 2000}  # in draw order: dev and test first
SEEDS = Path("data/seeds")


def generate() -> dict[str, list[int]]:
    paper = {int(s) for s in (SEEDS / "paper-25.txt").read_text(encoding="utf-8").split()}
    rng = random.Random(MASTER_SEED)
    seen = set(paper)
    lists: dict[str, list[int]] = {name: [] for name in COUNTS}
    for name, count in COUNTS.items():
        while len(lists[name]) < count:
            s = rng.randrange(10**12, 2 * 10**12)  # same magnitude as Tribes' stock seeds (Java long)
            if s not in seen:
                seen.add(s)
                lists[name].append(s)
    return lists


def main() -> int:
    lists = generate()
    for name, seeds in lists.items():
        path = SEEDS / f"{name}.txt"
        text = "\n".join(map(str, seeds)) + "\n"
        if "--check" in sys.argv[1:]:
            if path.read_text(encoding="utf-8") != text:
                print(f"{path} differs from the generator", file=sys.stderr)
                return 1
        else:
            path.write_text(text, encoding="utf-8")
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
