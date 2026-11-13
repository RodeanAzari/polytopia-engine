"""The policy/value network of ADR-011 §2 over the compact features of ADR-011 §1 (amended): the model itself
expands the per-tile classes into one-hot planes, so C++ inference and Python training share one expansion.

Input (as tools/selfplay records them, batched): fields uint8 [N, T, 9], flags uint8 [N, T], scalars float
[N, T, 4], global float [N, 80], with T = side * side and tile = x * side + y.
Output: policy logits [N, 332 * side * side + 130] in poly::network_index order (spatial channel-major, then
the 130 global logits), and value [N] in [-1, 1] from the mover's view.
"""

from __future__ import annotations

import torch
import torch.nn.functional as F
from records import FIELDS, GLOBAL, SCALARS, TILE_CLASSES
from torch import nn

FLAGS = 5  # features.hpp flag::kCount
POLICY_CHANNELS = 332  # ADR-006
POLICY_GLOBAL = 130
TILE_CHANNELS = sum(TILE_CLASSES) + FLAGS + SCALARS  # 97 + 5 + 4 = 106
_CLASS_OFFSETS = [sum(TILE_CLASSES[:f]) for f in range(FIELDS)]


def expand(fields: torch.Tensor, flags: torch.Tensor, scalars: torch.Tensor, side: int) -> torch.Tensor:
    """[N, T, 9] classes + [N, T] flag bits + [N, T, 4] scalars -> [N, 106, side, side] planes.

    Channel order: field 0's classes, field 1's, ..., then the 5 flag bits (bit 0 first), then the 4 scalars.
    """
    n, t, _ = fields.shape
    offsets = torch.tensor(_CLASS_OFFSETS, device=fields.device)
    onehot = torch.zeros(n, t, sum(TILE_CLASSES), device=fields.device, dtype=scalars.dtype)
    onehot.scatter_(2, fields.long() + offsets, 1.0)
    bits = (flags.long().unsqueeze(-1) >> torch.arange(FLAGS, device=flags.device)) & 1
    planes = torch.cat([onehot, bits.to(scalars.dtype), scalars], dim=2)  # [N, T, 106]
    return planes.transpose(1, 2).reshape(n, TILE_CHANNELS, side, side)


class Block(nn.Module):
    def __init__(self, c: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(c, c, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(c)
        self.conv2 = nn.Conv2d(c, c, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(c)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return F.relu(x + self.bn2(self.conv2(F.relu(self.bn1(self.conv1(x))))))


class PolyNet(nn.Module):
    def __init__(self, channels: int = 64, blocks: int = 6) -> None:
        super().__init__()
        self.channels, self.blocks = channels, blocks
        self.stem = nn.Conv2d(TILE_CHANNELS, channels, 3, padding=1, bias=False)
        self.stem_global = nn.Linear(GLOBAL, channels)  # global features, broadcast over the board
        self.stem_bn = nn.BatchNorm2d(channels)
        self.trunk = nn.Sequential(*[Block(channels) for _ in range(blocks)])
        self.policy_spatial = nn.Conv2d(channels, POLICY_CHANNELS, 1)
        self.policy_global = nn.Linear(channels, POLICY_GLOBAL)
        self.value_hidden = nn.Linear(channels, channels)
        self.value_out = nn.Linear(channels, 1)

    def forward(self, fields: torch.Tensor, flags: torch.Tensor, scalars: torch.Tensor, global_: torch.Tensor,
                side: int) -> tuple[torch.Tensor, torch.Tensor]:
        x = expand(fields, flags, scalars, side)
        x = self.stem(x) + self.stem_global(global_)[:, :, None, None]
        h = self.trunk(F.relu(self.stem_bn(x)))
        pooled = h.mean(dim=(2, 3))
        logits = torch.cat([self.policy_spatial(h).flatten(1), self.policy_global(pooled)], dim=1)
        value = torch.tanh(self.value_out(F.relu(self.value_hidden(pooled)))).squeeze(1)
        return logits, value


def policy_loss(logits: torch.Tensor, edge_index: torch.Tensor, edge_visits: torch.Tensor,
                edge_mask: torch.Tensor) -> torch.Tensor:
    """Cross-entropy between the visit distribution over each decision's root edges and the network's softmax
    over those same edges (ADR-011 §2-3). edge_* are [N, K], padded; padding has mask False. Mean over N."""
    gathered = logits.gather(1, edge_index.long()).masked_fill(~edge_mask, float("-inf"))
    logp = F.log_softmax(gathered, dim=1).masked_fill(~edge_mask, 0.0)
    visits = edge_visits.float() * edge_mask
    target = visits / visits.sum(dim=1, keepdim=True).clamp_min(1.0)
    return -(target * logp).sum(dim=1).mean()


def value_loss(value: torch.Tensor, z: torch.Tensor) -> torch.Tensor:
    return F.mse_loss(value, z)
