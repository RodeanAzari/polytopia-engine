"""tools/plot_headline.py scoring. Expected values come from ADR-007 §3 (score = W + D/2 from PUCT's side,
whichever seat it played; result0 is seat 0's result) and the Wilson interval's closed form."""

import csv
import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import plot_headline

FIELDS = ["obs", "agent0", "agent1", "result0"]


def write(tmp_path: Path, rows: list[tuple[str, str, str, str]]) -> str:
    p = tmp_path / "cell.csv"
    with p.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(FIELDS)
        w.writerows(rows)
    return str(p)


def test_seats_are_flipped_and_draws_count_half(tmp_path: Path):
    rows = [
        ("full", "PUCT", "RHEA", "WIN"),  # PUCT seat 0 wins: 1
        ("full", "RHEA", "PUCT", "WIN"),  # RHEA seat 0 wins: PUCT 0
        ("full", "RHEA", "PUCT", "LOSS"),  # PUCT seat 1 wins: 1
        ("full", "PUCT", "RHEA", "DRAW"),  # 0.5
        ("partial", "PUCT", "RHEA", "LOSS"),  # another condition: 0
        ("full", "PUCT:macro=0", "RHEA", "WIN"),  # another variant: 1
    ]
    t = plot_headline.tally([write(tmp_path, rows)])
    assert t[("full", "PUCT", "RHEA")] == (2.5, 4, 1)
    assert t[("partial", "PUCT", "RHEA")] == (0.0, 1, 0)
    assert t[("full", "PUCT:macro=0", "RHEA")] == (1.0, 1, 0)


def test_wilson_closed_form():
    lo, hi = plot_headline.wilson(155, 200)
    p, n, z = 155 / 200, 200, 1.96
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    assert (lo, hi) == pytest.approx((centre - half, centre + half))
    assert lo == pytest.approx(0.712, abs=5e-4)  # the RHEA Paper cell's summary: 71.2%–82.7%
    assert hi == pytest.approx(0.827, abs=5e-4)


def test_every_interval_must_clear_half(tmp_path: Path, capsys: pytest.CaptureFixture[str]):
    wins = [(c, "PUCT", o, "WIN") for c in ("full", "partial") for o in plot_headline.OPPONENTS for _ in range(30)]
    assert plot_headline.table(plot_headline.tally([write(tmp_path, wins)]))
    missing = [r for r in wins if not (r[0] == "partial" and r[2] == "OSLA")]  # one default cell not played
    assert not plot_headline.table(plot_headline.tally([write(tmp_path, missing)]))
    coin = wins + [("full", "PUCT", "MC", "LOSS")] * 25  # MC: 30 of 55, interval straddles 50%
    assert not plot_headline.table(plot_headline.tally([write(tmp_path, coin)]))
    capsys.readouterr()
