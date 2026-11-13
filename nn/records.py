"""Reader for the training records that tools/selfplay writes (layout: selfplay/record.hpp). NumPy only, so
CI can test it without PyTorch. Features are read as written; nothing here re-derives them (ADR-011 §5).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAGIC = b"PLYREC1\0"
RECORD_VERSION = 1
FIELDS = 9  # features.hpp field::kCount
SCALARS = 4  # scalar::kCount
GLOBAL = 80  # gfeat::kSize
TILE_CLASSES = (8, 8, 20, 10, 9, 13, 9, 7, 13)  # features.hpp kTileClasses
_HEADER = struct.Struct("<8sIIIIQQBBbBII")  # through the decision count; then u32 spec length


@dataclass
class Game:
    features_version: int
    side: int
    tribes: int
    level_seed: int
    game_seed: int
    partial: bool
    score_mode: bool
    winner: int  # -1: none (a draw)
    draw: bool
    ticks: int
    agent: str
    to_move: np.ndarray  # uint8 [n]
    fields: np.ndarray  # uint8 [n, side * side, FIELDS]
    flags: np.ndarray  # uint8 [n, side * side]
    scalars: np.ndarray  # float32 [n, side * side, SCALARS]
    global_: np.ndarray  # float32 [n, GLOBAL]
    edges: list[np.ndarray]  # per decision: uint32 [k, 2] = (network index, visits)

    def value_targets(self) -> np.ndarray:
        """Result from each decision's mover's view: +1 win, -1 loss, 0 draw (ADR-011 §3)."""
        if self.draw:
            return np.zeros(len(self.to_move), dtype=np.float32)
        return np.where(self.to_move == self.winner, 1.0, -1.0).astype(np.float32)


def read_game(path: str | Path) -> Game:
    """Parses one record file. Raises ValueError on a bad magic or version, truncation or trailing bytes."""
    buf = Path(path).read_bytes()
    if len(buf) < _HEADER.size + 4:
        raise ValueError(f"{path}: truncated header")
    (magic, version, fver, side, tribes, level_seed, game_seed, obs, mode, winner, draw, ticks,
     n) = _HEADER.unpack_from(buf, 0)
    if magic != MAGIC:
        raise ValueError(f"{path}: bad magic")
    if version != RECORD_VERSION:
        raise ValueError(f"{path}: unknown record version {version}")
    pos = _HEADER.size
    (spec_len,) = struct.unpack_from("<I", buf, pos)
    pos += 4
    agent = buf[pos:pos + spec_len].decode()
    pos += spec_len
    tiles = side * side
    size = 4 + tiles * FIELDS + tiles + tiles * SCALARS * 4 + GLOBAL * 4  # without the edges
    to_move = np.empty(n, np.uint8)
    fields = np.empty((n, tiles, FIELDS), np.uint8)
    flags = np.empty((n, tiles), np.uint8)
    scalars = np.empty((n, tiles, SCALARS), np.float32)
    global_ = np.empty((n, GLOBAL), np.float32)
    edges: list[np.ndarray] = []
    for i in range(n):
        if pos + size > len(buf):
            raise ValueError(f"{path}: truncated at decision {i}")
        to_move[i], _searched, k = struct.unpack_from("<BBH", buf, pos)
        p = pos + 4
        fields[i] = np.frombuffer(buf, np.uint8, tiles * FIELDS, p).reshape(tiles, FIELDS)
        p += tiles * FIELDS
        flags[i] = np.frombuffer(buf, np.uint8, tiles, p)
        p += tiles
        scalars[i] = np.frombuffer(buf, "<f4", tiles * SCALARS, p).reshape(tiles, SCALARS)
        p += tiles * SCALARS * 4
        global_[i] = np.frombuffer(buf, "<f4", GLOBAL, p)
        p += GLOBAL * 4
        if p + 8 * k > len(buf):
            raise ValueError(f"{path}: truncated edges at decision {i}")
        edges.append(np.frombuffer(buf, "<u4", 2 * k, p).reshape(k, 2).copy())
        pos = p + 8 * k
    if pos != len(buf):
        raise ValueError(f"{path}: {len(buf) - pos} trailing bytes")
    return Game(fver, side, tribes, level_seed, game_seed, obs == 1, mode == 1, winner, draw == 1, ticks, agent,
                to_move, fields, flags, scalars, global_, edges)
