#!/usr/bin/env python3
"""Exports a PolyNet checkpoint (nn/train.py) as TorchScript for the C++ evaluator (ADR-012 §1,
selfplay/torch_evaluator.hpp), for one map side. The exported module runs the network in fp16 on the GPU
and gathers each position's edge logits itself, so only [N, K] logits cross back to the CPU:

  forward(fields u8 [N, T, 9], flags u8 [N, T], scalars f32 [N, T, 4], global f32 [N, 80],
          edges i64 [N, K]) -> (edge logits f32 [N, K], value f32 [N])

Padding edges may hold any valid output index; the caller ignores their logits.
Usage: export.py <checkpoint.pt | random> <out.ts> [--side 11] [--channels 64 --blocks 6 (random only)]
"""

from __future__ import annotations

import argparse

import torch
from model import PolyNet
from pack import FEATURES_VERSION


class Exported(torch.nn.Module):
    def __init__(self, net: PolyNet, side: int) -> None:
        super().__init__()
        self.net = net
        self.side = side

    def forward(self, fields: torch.Tensor, flags: torch.Tensor, scalars: torch.Tensor, global_: torch.Tensor,
                edges: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        logits, value = self.net(fields, flags, scalars.half(), global_.half(), self.side)
        return logits.gather(1, edges).float(), value.float()


def build(checkpoint: str, side: int, channels: int, blocks: int) -> PolyNet:
    if checkpoint == "random":
        torch.manual_seed(0)
        return PolyNet(channels, blocks)
    ck = torch.load(checkpoint, map_location="cpu")
    if ck["features_version"] != FEATURES_VERSION:
        raise SystemExit(f"export: checkpoint has features version {ck['features_version']}")
    if ck["side"] != side:
        raise SystemExit(f"export: checkpoint trained at side {ck['side']}, asked for {side}")
    net = PolyNet(ck["channels"], ck["blocks"])
    net.load_state_dict(ck["model"])
    return net


def export(net: PolyNet, side: int, out: str) -> None:
    m = Exported(net.cuda().half().eval(), side).eval()
    t = side * side
    example = (torch.zeros(2, t, 9, dtype=torch.uint8, device="cuda"),
               torch.zeros(2, t, dtype=torch.uint8, device="cuda"),
               torch.zeros(2, t, 4, device="cuda"), torch.zeros(2, 80, device="cuda"),
               torch.zeros(2, 3, dtype=torch.int64, device="cuda"))
    with torch.no_grad():
        traced = torch.jit.trace(m, example, check_trace=False)
    traced.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("checkpoint")
    ap.add_argument("out")
    ap.add_argument("--side", type=int, default=11)
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--blocks", type=int, default=6)
    a = ap.parse_args()
    export(build(a.checkpoint, a.side, a.channels, a.blocks), a.side, a.out)
    print(f"export: {a.checkpoint} -> {a.out} (side {a.side})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
