"""tools/sprt.py (ADR-007 §5). Expected values come from closed forms and the method's definition, not from the
code: the constrained MLE on two support points is a Bernoulli/binomial, its optimality and mean constraint,
fishtest's documented small-effect approximation, the SPRT bounds, and the pair construction from a CSV."""

import csv
import math
import random
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import sprt


def test_bounds_at_five_percent():  # ADR-007 §5: about ±2.94
    lo, hi = sprt.bounds(0.05, 0.05)
    assert lo == pytest.approx(math.log(0.05 / 0.95))
    assert hi == pytest.approx(math.log(0.95 / 0.05))
    assert hi == pytest.approx(2.944, abs=1e-3)
    # alpha (false accept of H1) sets the upper bound, beta the lower: asymmetric case.
    lo, hi = sprt.bounds(0.05, 0.10)
    assert lo == pytest.approx(math.log(0.10 / 0.95))
    assert hi == pytest.approx(math.log(0.90 / 0.05))


def test_logistic_score():
    assert sprt.logistic_score(0) == 0.5
    assert sprt.logistic_score(400) == pytest.approx(10 / 11)
    assert sprt.logistic_score(-20) == pytest.approx(1 - sprt.logistic_score(20))


def test_mle_on_two_points_is_bernoulli():
    # Only 0 and 1 in the support: the only distribution with mean s is (1 - s, s).
    p = sprt.mle_expected([0.0, 1.0], [0.3, 0.7], 0.55)
    assert p == pytest.approx([0.45, 0.55], abs=1e-12)


def test_mle_has_the_mean_and_maximises_the_likelihood():
    rng = random.Random(1)
    values = list(sprt.VALUES)
    for _ in range(50):
        raw = [rng.random() + 0.01 for _ in values]
        phat = [r / sum(raw) for r in raw]
        s = rng.uniform(0.2, 0.8)
        p = sprt.mle_expected(values, phat, s)
        assert sum(p) == pytest.approx(1.0, abs=1e-12)
        assert sum(pi * v for pi, v in zip(p, values)) == pytest.approx(s, abs=1e-12)
        best = sum(ph * math.log(pi) for ph, pi in zip(phat, p))
        for _ in range(20):  # any feasible perturbation (sum 0, mean 0) lowers the likelihood
            d = [rng.uniform(-1, 1) for _ in values]
            # project onto {sum d = 0, sum d v = 0}
            for basis in ([1.0] * 5, [v - 0.5 for v in values]):
                k = sum(x * y for x, y in zip(d, basis)) / sum(y * y for y in basis)
                d = [x - k * y for x, y in zip(d, basis)]
            eps = 1e-3 * min(p) / max(abs(x) for x in d)
            q = [pi + eps * x for pi, x in zip(p, d)]
            assert sum(ph * math.log(qi) for ph, qi in zip(phat, q)) < best


def test_llr_on_two_point_data_is_the_binomial_llr():
    # Pairs scoring only 0 or 2 (cells 0 and 4) behave like coin flips; the other cells are regularised to 1e-3,
    # which moves the LLR only slightly.
    k, n = 120, 200
    s0, s1 = sprt.logistic_score(0), sprt.logistic_score(20)
    binomial = k * math.log(s1 / s0) + (n - k) * math.log((1 - s1) / (1 - s0))
    assert sprt.llr_logistic(0, 20, [n - k, 0, 0, 0, k]) == pytest.approx(binomial, rel=1e-3)


def test_llr_matches_the_small_effect_approximation():
    # fishtest's GSPRT approximation: N (s1 - s0)(2 s - s0 - s1) / (2 var), close for small effects and large N.
    counts = [1187, 7410, 13475, 7378, 1164]
    n = sum(counts)
    mean = sum(c * v for c, v in zip(counts, sprt.VALUES)) / n
    var = sum(c * (v - mean) ** 2 for c, v in zip(counts, sprt.VALUES)) / n
    s0, s1 = sprt.logistic_score(-3), sprt.logistic_score(1)
    approx = n * (s1 - s0) * (2 * mean - s0 - s1) / (2 * var)
    assert sprt.llr_logistic(-3, 1, counts) == pytest.approx(approx, rel=0.01)


def test_llr_sign_and_symmetry():
    good = [5, 20, 40, 30, 15]  # A scores well above 50%
    assert sprt.llr_logistic(0, 20, good) > 0
    assert sprt.llr_logistic(0, 20, good[::-1]) < 0
    assert sprt.llr_logistic(10, 10, good) == pytest.approx(0.0, abs=1e-9)
    # Swapping the hypotheses negates the LLR.
    assert sprt.llr_logistic(20, 0, good) == pytest.approx(-sprt.llr_logistic(0, 20, good))


def test_llr_with_empty_cells_is_finite():
    assert math.isfinite(sprt.llr_logistic(0, 20, [0, 0, 10, 0, 0]))
    assert math.isfinite(sprt.llr_logistic(0, 20, [0, 5, 20, 10, 3]))


def test_elo_estimate():
    elo, lo, hi = sprt.elo_estimate([10, 20, 40, 20, 10])
    assert elo == pytest.approx(0.0, abs=1e-9)
    assert lo < 0 < hi
    elo, _, _ = sprt.elo_estimate([0, 0, 0, 10, 0])  # mean 3/4 -> 400 log10(3)
    assert elo == pytest.approx(400 * math.log10(3))


def write_csv(path: Path, rows: list[tuple[str, str, str, str, str]]) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["game", "levelSeed", "rep", "agent0", "agent1", "result0"])
        for i, (level, rep, a0, a1, r0) in enumerate(rows):
            w.writerow([i, level, rep, a0, a1, r0])


def test_pentanomial_from_pairs(tmp_path: Path):
    p = tmp_path / "t.csv"
    write_csv(p, [
        ("1", "0", "X", "Y", "WIN"), ("1", "0", "Y", "X", "LOSS"),   # X 2/2 -> cell 4
        ("1", "1", "X", "Y", "WIN"), ("1", "1", "Y", "X", "WIN"),    # X 1/2 -> cell 2
        ("2", "0", "X", "Y", "DRAW"), ("2", "0", "Y", "X", "WIN"),   # X 0.5/2 -> cell 1
        ("2", "1", "X", "Y", "LOSS"), ("2", "1", "Y", "X", "WIN"),   # X 0/2 -> cell 0
        ("3", "0", "X", "Y", "WIN"),                                 # incomplete
        ("3", "0", "X", "Z", "WIN"), ("3", "0", "Z", "X", "LOSS"),   # other pairing: ignored
    ])
    counts, incomplete = sprt.pentanomial(str(p), "X", "Y")
    assert counts == [1, 1, 1, 0, 1]
    assert incomplete == 1
    counts_y, _ = sprt.pentanomial(str(p), "Y", "X")
    assert counts_y == counts[::-1]
