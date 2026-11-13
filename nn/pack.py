#!/usr/bin/env python3
"""Packs a self-play run's record files (<run>/<game>.rec) into memory-mappable arrays for training, in
<run>/packed/ (one .npy per array; decision i's root edges are edge_*[edge_start[i]:edge_start[i + 1]]):

  fields u8 [N, T, 9]   flags u8 [N, T]   scalars f16 [N, T, 4]   global f32 [N, 80]
  to_move u8 [N]   z f32 [N] (result from the mover's view)   game i32 [N]   partial u8 [N]
  edge_start i64 [N + 1]   edge_index u32 [E]   edge_visits u32 [E]

Games are packed in game-number order; all must share one side and the current features version.
Usage: pack.py <run dir>
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import records
from numpy.lib.format import open_memmap

FEATURES_VERSION = 1  # features.hpp kFeaturesVersion


def game_files(run: Path) -> list[Path]:
    return sorted(run.glob("*.rec"), key=lambda p: int(p.stem))


def pack(run: Path) -> Path:
    files = game_files(run)
    if not files:
        raise SystemExit(f"pack: no records in {run}")
    n = e = 0
    side = None
    for f in files:
        g = records.read_game(f)
        if g.features_version != FEATURES_VERSION:
            raise SystemExit(f"pack: {f} has features version {g.features_version}, want {FEATURES_VERSION}")
        if side is not None and g.side != side:
            raise SystemExit(f"pack: {f} has side {g.side}, the others {side}")
        side = g.side
        n += len(g.to_move)
        e += sum(len(x) for x in g.edges)
    assert side is not None
    tiles = side * side
    out = run / "packed"
    out.mkdir(exist_ok=True)

    def mm(name: str, dtype: str, shape: tuple[int, ...]) -> np.ndarray:
        return open_memmap(out / f"{name}.npy", mode="w+", dtype=dtype, shape=shape)

    arrays = {
        "fields": mm("fields", "u1", (n, tiles, records.FIELDS)),
        "flags": mm("flags", "u1", (n, tiles)),
        "scalars": mm("scalars", "f2", (n, tiles, records.SCALARS)),
        "global": mm("global", "f4", (n, records.GLOBAL)),
        "to_move": mm("to_move", "u1", (n,)),
        "z": mm("z", "f4", (n,)),
        "game": mm("game", "i4", (n,)),
        "partial": mm("partial", "u1", (n,)),
        "edge_start": mm("edge_start", "i8", (n + 1,)),
        "edge_index": mm("edge_index", "u4", (e,)),
        "edge_visits": mm("edge_visits", "u4", (e,)),
    }
    i = j = 0
    for f in files:
        g = records.read_game(f)
        k = len(g.to_move)
        arrays["fields"][i:i + k] = g.fields
        arrays["flags"][i:i + k] = g.flags
        arrays["scalars"][i:i + k] = g.scalars.astype(np.float16)
        arrays["global"][i:i + k] = g.global_
        arrays["to_move"][i:i + k] = g.to_move
        arrays["z"][i:i + k] = g.value_targets()
        arrays["game"][i:i + k] = int(f.stem)
        arrays["partial"][i:i + k] = int(g.partial)
        for d, edges in enumerate(g.edges):
            arrays["edge_start"][i + d] = j
            arrays["edge_index"][j:j + len(edges)] = edges[:, 0]
            arrays["edge_visits"][j:j + len(edges)] = edges[:, 1]
            j += len(edges)
        i += k
    arrays["edge_start"][n] = j
    for a in arrays.values():
        a.flush()
    (out / "side.txt").write_text(f"{side}\n")
    print(f"pack: {len(files)} games, {n} decisions, {e} edges, side {side} -> {out}")
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    pack(Path(sys.argv[1]))
