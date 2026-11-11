#!/usr/bin/env python3
"""Pentanomial GSPRT for engine changes (ADR-007 §5): A (the change) vs. B (the previous version) on seat-swapped
game pairs, H0: Elo = elo0 vs. H1: Elo = elo1 in logistic Elo, error rates alpha and beta.

Method (fishtest's, reimplemented from its description, not its code): a pair's score is A's mean over its two
games, one of {0, 1/4, 1/2, 3/4, 1}; empty pentanomial cells count 1e-3; the log-likelihood ratio is
N * mean over the observed pairs of ln(p1/p0), where p_k is the maximum-likelihood pentanomial distribution with
expected score s_k = 1 / (1 + 10^(-elo_k/400)) (constrained multinomial MLE: p_i = phat_i / (1 + x (a_i - s)),
x the root of the secular equation sum_i phat_i (a_i - s) / (1 + x (a_i - s)) = 0). Stop when the LLR leaves
[ln(beta/(1-alpha)), ln((1-beta)/alpha)]; ADR-007 caps a test at 2,000 pairs (inconclusive, not a pass).

Usage (repo root): sprt.py <tournament.csv> <agent A> <agent B> [--elo0=0] [--elo1=20] [--alpha=0.05]
                   [--beta=0.05] [--cap=2000]
Exit status: 0 = H1 accepted (A better), 1 = H0 accepted, 2 = continue, 3 = cap reached (inconclusive).
"""

import csv
import math
import sys
from collections import defaultdict

VALUES = (0.0, 0.25, 0.5, 0.75, 1.0)
EMPTY_CELL = 1e-3


def logistic_score(elo: float) -> float:
    return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))


def bounds(alpha: float, beta: float) -> tuple[float, float]:
    return math.log(beta / (1.0 - alpha)), math.log((1.0 - beta) / alpha)


def mle_expected(values: list[float], probs: list[float], s: float) -> list[float]:
    """The distribution on `values` with mean s maximising sum_i probs_i ln p_i (all probs_i > 0)."""
    d = [a - s for a in values]
    if not min(d) < 0.0 < max(d):
        raise ValueError(f"score {s} outside the support")
    lo, hi = -1.0 / max(d), -1.0 / min(d)  # 1 + x d_i > 0 for every i strictly inside

    def f(x: float) -> float:  # strictly decreasing on (lo, hi)
        return sum(p * di / (1.0 + x * di) for p, di in zip(probs, d, strict=True))

    a, b = lo + 1e-12 * (hi - lo), hi - 1e-12 * (hi - lo)
    for _ in range(200):
        m = 0.5 * (a + b)
        if f(m) > 0.0:
            a = m
        else:
            b = m
    x = 0.5 * (a + b)
    return [p / (1.0 + x * di) for p, di in zip(probs, d, strict=True)]


def llr_logistic(elo0: float, elo1: float, counts: list[int]) -> float:
    """GSPRT log-likelihood ratio of H1 vs. H0 for pentanomial `counts` (pairs scoring 0, 1/2, 1, 3/2, 2)."""
    reg = [c if c > 0 else EMPTY_CELL for c in counts]
    n = sum(reg)
    phat = [c / n for c in reg]
    p0 = mle_expected(list(VALUES), phat, logistic_score(elo0))
    p1 = mle_expected(list(VALUES), phat, logistic_score(elo1))
    return n * sum(ph * (math.log(b) - math.log(a)) for ph, a, b in zip(phat, p0, p1, strict=True))


def elo_estimate(counts: list[int]) -> tuple[float, float, float]:
    """Logistic Elo of A from the mean pair score, with a 95% interval from the pentanomial variance."""
    n = sum(counts)
    mean = sum(c * v for c, v in zip(counts, VALUES, strict=True)) / n
    var = sum(c * (v - mean) ** 2 for c, v in zip(counts, VALUES, strict=True)) / n
    half = 1.96 * math.sqrt(var / n)

    def elo(s: float) -> float:
        s = min(max(s, 1e-9), 1 - 1e-9)
        return -400.0 * math.log10(1.0 / s - 1.0)

    return elo(mean), elo(mean - half), elo(mean + half)


def pentanomial(path: str, a: str, b: str) -> tuple[list[int], int]:
    """Pentanomial counts of A vs. B from a tools/tournament CSV; pairs are (level seed, rep) with A in both seats.
    Returns the counts and the number of incomplete pairs skipped."""
    games: dict[tuple[str, str], list[float]] = defaultdict(list)
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if {r["agent0"], r["agent1"]} != {a, b}:
                continue
            s0 = {"WIN": 1.0, "DRAW": 0.5, "LOSS": 0.0}[r["result0"]]
            games[(r["levelSeed"], r["rep"])].append(s0 if r["agent0"] == a else 1.0 - s0)
    counts = [0] * 5
    incomplete = 0
    for scores in games.values():
        if len(scores) == 2:
            counts[round(2 * sum(scores))] += 1
        else:
            incomplete += 1
    return counts, incomplete


def opt(name: str, default: float) -> float:
    return float(next((x.split("=", 1)[1] for x in sys.argv[4:] if x.startswith(f"--{name}=")), default))


def main() -> int:
    path, a, b = sys.argv[1:4]
    elo0, elo1, alpha, beta, cap = opt("elo0", 0), opt("elo1", 20), opt("alpha", 0.05), opt("beta", 0.05), opt("cap", 2000)
    counts, incomplete = pentanomial(path, a, b)
    n = sum(counts)
    if n == 0:
        print("no complete pairs")
        return 2
    lower, upper = bounds(alpha, beta)
    llr = llr_logistic(elo0, elo1, counts)
    elo, elo_lo, elo_hi = elo_estimate(counts)
    if llr >= upper:
        verdict, code = "H1 accepted (A better)", 0
    elif llr <= lower:
        verdict, code = "H0 accepted", 1
    elif n >= cap:
        verdict, code = "cap reached: inconclusive", 3
    else:
        verdict, code = "continue", 2
    print(f"{a} vs {b}: {n} pairs ({incomplete} incomplete skipped), pentanomial {counts}")
    print(f"H0 {elo0:g} vs H1 {elo1:g} logistic Elo, alpha {alpha:g}, beta {beta:g}: LLR {llr:.3f} "
          f"in ({lower:.3f}, {upper:.3f}) -> {verdict}")
    print(f"Elo estimate {elo:+.1f} (95% {elo_lo:+.1f} to {elo_hi:+.1f})")
    return code


if __name__ == "__main__":
    sys.exit(main())
