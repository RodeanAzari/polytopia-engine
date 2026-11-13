"""nn/pack.py on record files assembled byte by byte (test_records.py's writer, from selfplay/record.hpp)."""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "nn"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack
from test_records import SIDE, TILES, decision, header


def test_pack_concatenates_games_in_order(tmp_path: Path):
    (tmp_path / "10.rec").write_bytes(header(1, winner=0) + decision(1, 3, [(4, 2), (6, 0), (8, 1)]))
    (tmp_path / "2.rec").write_bytes(header(2, winner=1) + decision(0, 0, [(1, 1)]) + decision(1, 1, [(2, 5)]))
    out = pack.pack(tmp_path)

    def load(name: str) -> np.ndarray:
        return np.load(out / f"{name}.npy")

    assert load("game").tolist() == [2, 2, 10]  # numeric order: 2 before 10
    assert load("z").tolist() == [-1.0, 1.0, -1.0]  # winner 1, then winner 0 with tribe 1 to move
    assert load("edge_start").tolist() == [0, 1, 2, 5]
    assert load("edge_index").tolist() == [1, 2, 4, 6, 8]
    assert load("edge_visits").tolist() == [1, 5, 2, 0, 1]
    assert load("fields").shape == (3, TILES, 9) and load("fields")[2, 0, 0] == 3 % 7
    assert load("partial").tolist() == [1, 1, 1]
    assert (out / "side.txt").read_text().strip() == str(SIDE)
