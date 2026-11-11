"""tools/replay_gif.py frame selection. Expectations come from the replay format (tools/record.cpp: every frame
has t, p and e; vc only on searched decisions) and the option meanings in the script's usage text."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import replay_gif


def frame(t: int, p: int, e: str, vc: bool = False) -> dict:
    f = {"t": t, "p": p, "e": e}
    if vc:
        f["vc"] = [[0, 0, 1, 1, 10, "x"]]
    return f


# load, then two turns of tribe 0 / tribe 1 each with one action; tribe 0 searched (vc)
FRAMES = [
    frame(0, -1, "load"),
    frame(0, 0, "start"),
    frame(0, 0, "action", vc=True),
    frame(0, 0, "end", vc=True),
    frame(0, 1, "start"),
    frame(0, 1, "action"),
    frame(0, 1, "end"),
    frame(1, 0, "start"),
    frame(1, 0, "action", vc=True),
    frame(1, 0, "end"),
    frame(1, 1, "start"),
    frame(1, 1, "action"),
    frame(1, 1, "end"),
]


def test_all_frames_are_one_based():
    assert replay_gif.select_frames(FRAMES, None, None, False) == list(range(1, 14))


def test_turn_range_is_inclusive():
    assert replay_gif.select_frames(FRAMES, (1, 1), None, False) == list(range(8, 14))
    assert replay_gif.select_frames(FRAMES, (0, 0), None, False) == list(range(1, 8))


def test_tribe_keeps_only_its_turns():
    assert replay_gif.select_frames(FRAMES, None, 1, False) == [5, 6, 7, 11, 12, 13]


def test_decisions_only_keeps_searched_frames_and_turn_starts():
    assert replay_gif.select_frames(FRAMES, None, 0, True) == [2, 3, 4, 8, 9]


def test_filters_combine():
    assert replay_gif.select_frames(FRAMES, (1, 1), 0, True) == [8, 9]


@pytest.mark.parametrize("spec", ["3", "3-", "-3", "a-b", "5-2", "1-2-3"])
def test_bad_turn_specs_are_rejected(spec: str):
    with pytest.raises(ValueError):
        replay_gif.parse_turns(spec)


def test_turn_spec():
    assert replay_gif.parse_turns("2-9") == (2, 9)
    assert replay_gif.parse_turns(None) is None
