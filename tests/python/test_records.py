"""nn/records.py against files assembled here byte by byte from the layout in selfplay/record.hpp (an
independent writer, so a reader that merely mirrors the C++ writer's mistakes would fail)."""

import struct
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "nn"))
import records

SIDE = 3  # 9 tiles keeps the hand-built file small
TILES = SIDE * SIDE


def header(n: int, winner: int = 1, draw: int = 0, agent: bytes = b"PUCT", version: int = 1) -> bytes:
    return (b"PLYREC1\0" + struct.pack("<IIII", version, 1, SIDE, 2) + struct.pack("<QQ", 123456789012, 2**63 + 5)
            + struct.pack("<BBbB", 1, 0, winner, draw) + struct.pack("<II", 30, n)
            + struct.pack("<I", len(agent)) + agent)


def decision(to_move: int, base: int, edges: list[tuple[int, int]]) -> bytes:
    fields = bytes((base + i) % 7 for i in range(TILES * 9))
    flags = bytes((base + i) % 32 for i in range(TILES))
    scalars = struct.pack(f"<{TILES * 4}f", *[(base + i) / 8 for i in range(TILES * 4)])
    glob = struct.pack("<80f", *[-(base + i) for i in range(80)])
    tail = b"".join(struct.pack("<II", a, b) for a, b in edges)
    return struct.pack("<BBH", to_move, 1, len(edges)) + fields + flags + scalars + glob + tail


def write(tmp_path: Path, data: bytes) -> Path:
    p = tmp_path / "g.rec"
    p.write_bytes(data)
    return p


def test_reads_header_and_decisions(tmp_path: Path):
    data = header(2) + decision(0, 0, [(10, 3), (2989, 0)]) + decision(1, 5, [(7, 1)])
    g = records.read_game(write(tmp_path, data))
    assert (g.side, g.tribes, g.level_seed, g.game_seed) == (SIDE, 2, 123456789012, 2**63 + 5)
    assert g.partial and not g.score_mode and g.winner == 1 and not g.draw and g.ticks == 30
    assert g.agent == "PUCT"
    assert g.to_move.tolist() == [0, 1]
    assert g.fields.shape == (2, TILES, 9)
    assert g.fields[1, 0, 3] == (5 + 3) % 7  # decision 1, tile 0, field 3
    assert g.fields[0, 2, 1] == (2 * 9 + 1) % 7  # tile-major: tile 2, field 1
    assert g.flags[1, 4] == (5 + 4) % 32
    assert g.scalars[0, 1, 2] == pytest.approx((1 * 4 + 2) / 8)
    assert g.global_[1, 79] == -(5 + 79)
    assert g.edges[0].tolist() == [[10, 3], [2989, 0]]
    assert g.edges[1].tolist() == [[7, 1]]


def test_value_targets_are_from_the_movers_view(tmp_path: Path):
    data = header(2, winner=1) + decision(0, 0, [(1, 1)]) + decision(1, 0, [(1, 1)])
    assert records.read_game(write(tmp_path, data)).value_targets().tolist() == [-1.0, 1.0]
    draw = header(2, winner=-1, draw=1) + decision(0, 0, [(1, 1)]) + decision(1, 0, [(1, 1)])
    assert records.read_game(write(tmp_path, draw)).value_targets().tolist() == [0.0, 0.0]


def test_a_game_without_decisions(tmp_path: Path):
    g = records.read_game(write(tmp_path, header(0)))
    assert g.fields.shape == (0, TILES, 9) and g.edges == []


@pytest.mark.parametrize("damage", ["truncated", "trailing", "magic", "version", "edges"])
def test_damaged_files_are_rejected(tmp_path: Path, damage: str):
    good = header(1) + decision(0, 0, [(1, 2), (3, 4)])
    bad = {
        "truncated": good[:-1],
        "trailing": good + b"\0",
        "magic": b"X" + good[1:],
        "version": header(1, version=2) + decision(0, 0, [(1, 2), (3, 4)]),
        "edges": header(1) + decision(0, 0, [(1, 2), (3, 4)])[:-8],
    }[damage]
    with pytest.raises(ValueError):
        records.read_game(write(tmp_path, bad))


def test_class_counts_match_the_cpp_header():
    text = (Path(__file__).resolve().parents[2] / "sim/include/poly/features.hpp").read_text()
    assert "kTileClasses = {" + ", ".join(map(str, records.TILE_CLASSES)) + "}" in text
    assert np.sum(records.TILE_CLASSES) == 97  # ADR-011 §1 amendment: 97 one-hot channels
