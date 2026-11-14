"""C++ inference parity (ADR-011 §5, ADR-012 §1): build/torch/net_check runs an exported network through
TorchEvaluator (LibTorch, batched across threads) on recorded positions; PyTorch runs the same exported module on
the same positions. Values and edge logits must agree within fp16 tolerance. Needs PyTorch with CUDA and
`make build-nn`; skipped otherwise (CI has neither)."""

import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "nn"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_records import header

torch = pytest.importorskip("torch")
NET_CHECK = ROOT / "build/torch/net_check"
pytestmark = pytest.mark.skipif(not (torch.cuda.is_available() and NET_CHECK.exists()),
                                reason="needs CUDA and make build-nn")

import export
import records


def random_record(path: Path, n: int, side: int) -> None:
    """n decisions with random (valid) features and edges, written in the record layout."""
    rng = np.random.default_rng(7)
    tiles = side * side
    out = bytearray(header(n))
    out[16:20] = side.to_bytes(4, "little")
    for i in range(n):
        fields = np.stack([rng.integers(0, c, tiles) for c in records.TILE_CLASSES], axis=1).astype(np.uint8)
        flags = rng.integers(0, 32, tiles).astype(np.uint8)
        scalars = rng.random((tiles, 4)).astype("<f4")
        glob = rng.random(80).astype("<f4")
        k = int(rng.integers(1, 40))
        edges = rng.choice(332 * tiles + 130, size=k, replace=False)
        out += struct.pack("<BBH", i % 2, 1, k) + fields.tobytes() + flags.tobytes() + scalars.tobytes()
        out += glob.tobytes() + b"".join(struct.pack("<II", int(e), 1) for e in edges)
    path.write_bytes(bytes(out))


@pytest.mark.parametrize("threads,batch,graphs,pipelines", [(1, 1, 0, 1), (8, 64, 0, 1), (16, 7, 0, 1), (1, 1, 1, 1),
                                                            (8, 64, 1, 1), (16, 7, 1, 1), (40, 32, 1, 1),
                                                            # several batchers: each request answered with
                                                            # its own row while batches overlap
                                                            (40, 8, 1, 2), (64, 16, 1, 3), (16, 7, 0, 2)])
def test_cpp_matches_pytorch(tmp_path: Path, threads: int, batch: int, graphs: int, pipelines: int):
    side = 11
    model = tmp_path / "net.ts"
    export.export(export.build("random", side, 32, 2), side, str(model))
    rec = tmp_path / "g.rec"
    random_record(rec, 48, side)
    run = subprocess.run([str(NET_CHECK), str(model), str(rec), "48", str(threads), str(batch), str(graphs),
                          str(pipelines)],
                         capture_output=True, text=True, check=True)
    out = run.stdout.splitlines()
    replayed = int(run.stderr.split(" batches, ")[1].split()[0])
    assert (replayed > 0) == bool(graphs)  # the path under test was the one taken
    g = records.read_game(rec)
    m = torch.jit.load(str(model), map_location="cuda")
    assert len(out) == 48
    for i, line in enumerate(out):
        parts = line.split()
        assert int(parts[0]) == i
        k = len(g.edges[i])
        with torch.no_grad():
            logits, value = m(torch.from_numpy(g.fields[i:i + 1]).cuda(), torch.from_numpy(g.flags[i:i + 1]).cuda(),
                              torch.from_numpy(g.scalars[i:i + 1]).cuda(), torch.from_numpy(g.global_[i:i + 1]).cuda(),
                              torch.from_numpy(g.edges[i][:, 0].astype(np.int64))[None].cuda())
        cpp = np.array(parts[2:], dtype=np.float64)
        assert len(cpp) == k
        # Same fp16 module on the same GPU: measured differences (batch composition) ~5e-4.
        assert float(parts[1]) == pytest.approx(value.item(), abs=5e-3)
        np.testing.assert_allclose(cpp, logits[0].cpu().numpy(), atol=5e-3, rtol=0)


TOURNAMENT = ROOT / "build/torch/tournament"
DEV_LEVELS = ROOT / "build/levels/dev"


@pytest.mark.skipif(not (TOURNAMENT.exists() and DEV_LEVELS.is_dir()), reason="needs make build-nn and dev levels")
def test_tournament_with_a_network_agent_exits_cleanly(tmp_path: Path):
    # Regression (notebook 2026-11-14): the shared evaluators were static objects destroyed after the CUDA
    # runtime had unloaded, so a finished tournament aborted at exit ("driver shutting down") and the SPRT
    # script (set -e) stopped with its batch complete.
    side = 11
    model = tmp_path / "net.ts"
    export.export(export.build("random", side, 32, 2), side, str(model))
    levels = tmp_path / "levels"
    levels.mkdir()
    level = min(DEV_LEVELS.iterdir())
    (levels / level.name).write_bytes(level.read_bytes())
    out = tmp_path / "t.csv"
    run = subprocess.run([str(TOURNAMENT), f"--levels={levels}", f"--agents=PUCT:budget=20:net={model},Random",
                          "--reps=1", "--obs=partial", "--id=exit", f"--out={out}", "--jobs=2"],
                         capture_output=True, text=True, check=False)
    assert run.returncode == 0, run.stderr[-2000:]
    assert len(out.read_text().splitlines()) == 3  # header + both seatings
