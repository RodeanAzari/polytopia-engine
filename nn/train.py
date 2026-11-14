#!/usr/bin/env python3
"""Supervised training of PolyNet on packed records (ADR-011 §3-4): cross-entropy to the search's root visit
distribution + MSE to the game result, L2 by weight decay. Games are split train/validation by game number
(the last --val-games games validate), so no validation position shares a game with training.

Loss = policy + --value-weight x value. Writes <out>/best.pt (the evaluation with the lowest validation loss, by the
same weighting: with few games the value head memorises them, notebook 2026-11-13), <out>/step-<k>.pt checkpoints ({"model", "channels", "blocks", "side", "features_version", "step"}) and
<out>/log.csv (step, seconds, train losses, validation losses and policy top-1 agreement with the search).
Usage: train.py <packed dir> <out dir> [--steps 20000] [--batch 256] [--lr 0.02] [--channels 64] [--blocks 6]
                [--val-games 100] [--eval-every 1000] [--seed 1] [--value-weight 1.0]
"""

from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

import numpy as np
import torch
from model import PolyNet, policy_loss, value_loss
from pack import FEATURES_VERSION


class Packed:
    """The arrays of nn/pack.py, memory-mapped."""

    def __init__(self, path: Path) -> None:
        def load(name: str) -> np.ndarray:
            return np.load(path / f"{name}.npy", mmap_mode="r")

        self.side = int((path / "side.txt").read_text())
        self.fields, self.flags, self.scalars, self.global_ = (load(k) for k in ("fields", "flags", "scalars",
                                                                                 "global"))
        self.z, self.game = load("z"), load("game")
        self.edge_start, self.edge_index, self.edge_visits = (load(k) for k in ("edge_start", "edge_index",
                                                                                "edge_visits"))

    def batch(self, idx: np.ndarray, device: torch.device) -> dict[str, torch.Tensor]:
        idx = np.sort(idx)  # memory-map reads in order
        starts, ends = self.edge_start[idx], self.edge_start[idx + 1]
        k = int((ends - starts).max())
        ei = np.zeros((len(idx), k), np.int64)
        ev = np.zeros((len(idx), k), np.float32)
        mask = np.zeros((len(idx), k), bool)
        for r, (s, e) in enumerate(zip(starts, ends, strict=True)):
            ei[r, :e - s] = self.edge_index[s:e]
            ev[r, :e - s] = self.edge_visits[s:e]
            mask[r, :e - s] = True

        def t(a: np.ndarray, dtype: torch.dtype | None = None) -> torch.Tensor:
            x = torch.from_numpy(np.ascontiguousarray(a))
            return x.to(device, dtype=dtype, non_blocking=True)

        return {"fields": t(self.fields[idx]), "flags": t(self.flags[idx]),
                "scalars": t(self.scalars[idx], torch.float32), "global": t(self.global_[idx]),
                "z": t(self.z[idx]), "edge_index": t(ei), "edge_visits": t(ev), "edge_mask": t(mask)}


def losses(net: PolyNet, b: dict[str, torch.Tensor], side: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Policy loss, value loss, and top-1 agreement (the network's best edge is the search's most-visited)."""
    logits, value = net(b["fields"], b["flags"], b["scalars"], b["global"], side)
    lp = policy_loss(logits.float(), b["edge_index"], b["edge_visits"], b["edge_mask"])
    lv = value_loss(value.float(), b["z"])
    gathered = logits.float().gather(1, b["edge_index"]).masked_fill(~b["edge_mask"], float("-inf"))
    agree = (gathered.argmax(1) == b["edge_visits"].masked_fill(~b["edge_mask"], -1).argmax(1)).float().mean()
    return lp, lv, agree


@torch.no_grad()
def evaluate(net: PolyNet, data: Packed, idx: np.ndarray, device: torch.device, batch: int) -> list[float]:
    net.eval()
    sums = np.zeros(3)
    for s in range(0, len(idx), batch):
        part = idx[s:s + batch]
        with torch.autocast(device.type, dtype=torch.float16, enabled=device.type == "cuda"):
            out = losses(net, data.batch(part, device), data.side)
        sums += np.array([float(x) for x in out]) * len(part)
    net.train()
    return list(sums / len(idx))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("packed", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--steps", type=int, default=20000)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--lr", type=float, default=0.02)
    ap.add_argument("--channels", type=int, default=64)
    ap.add_argument("--blocks", type=int, default=6)
    ap.add_argument("--val-games", type=int, default=100)
    ap.add_argument("--eval-every", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--value-weight", type=float, default=1.0)
    a = ap.parse_args()

    torch.manual_seed(a.seed)
    rng = np.random.default_rng(a.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    data = Packed(a.packed)
    games = np.unique(data.game)
    val_games = games[-a.val_games:]
    is_val = np.isin(data.game, val_games)
    train_idx, val_idx = np.flatnonzero(~is_val), np.flatnonzero(is_val)
    print(f"train: {len(train_idx)} decisions from {len(games) - len(val_games)} games; "
          f"validation: {len(val_idx)} from {len(val_games)} games; side {data.side}; {device}")

    net = PolyNet(a.channels, a.blocks).to(device)
    opt = torch.optim.SGD(net.parameters(), lr=a.lr, momentum=0.9, weight_decay=1e-4, nesterov=True)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, a.steps)
    scaler = torch.amp.GradScaler(enabled=device.type == "cuda")
    a.out.mkdir(parents=True, exist_ok=True)
    log = (a.out / "log.csv").open("w", newline="")
    w = csv.writer(log)
    w.writerow(["step", "seconds", "train_policy", "train_value", "train_top1", "val_policy", "val_value",
                "val_top1"])
    t0 = time.time()
    run = np.zeros(3)
    best = float("inf")
    for step in range(1, a.steps + 1):
        b = data.batch(rng.choice(train_idx, a.batch, replace=False), device)
        with torch.autocast(device.type, dtype=torch.float16, enabled=device.type == "cuda"):
            lp, lv, agree = losses(net, b, data.side)
        opt.zero_grad(set_to_none=True)
        scaler.scale(lp + a.value_weight * lv).backward()
        scaler.step(opt)
        scaler.update()
        sched.step()
        run += [lp.item(), lv.item(), agree.item()]
        if step % a.eval_every == 0 or step == a.steps:
            n = a.eval_every if step % a.eval_every == 0 else step % a.eval_every
            val = evaluate(net, data, val_idx, device, 1024)
            row = [step, round(time.time() - t0, 1), *np.round(run / n, 4), *np.round(val, 4)]
            w.writerow(row)
            log.flush()
            print(" ".join(map(str, row)), flush=True)
            run[:] = 0
            ck = {"model": net.state_dict(), "channels": a.channels, "blocks": a.blocks, "side": data.side,
                  "features_version": FEATURES_VERSION, "step": step, "val": [float(x) for x in val]}
            torch.save(ck, a.out / f"step-{step}.pt")
            if val[0] + a.value_weight * val[1] < best:
                best = val[0] + a.value_weight * val[1]
                torch.save(ck, a.out / "best.pt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
