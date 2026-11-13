"""nn/model.py. Expected values come from ADR-011's layouts (class offsets 0, 8, 16, 36, 46, 55,
68, 77, 84 from the class counts; flags at 97..101; scalars at 102..105; policy index (ch * side + x) * side + y,
then the 130 global logits, as poly::network_index), not from the code. These tests need PyTorch (make setup-nn)
and are skipped without it (CI has no PyTorch)."""

import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "nn"))
import records

torch = pytest.importorskip("torch")
import model


def test_expand_puts_each_value_on_its_channel():
    side = 2
    fields = torch.tensor([[[c - 1 for c in records.TILE_CLASSES]] * 4], dtype=torch.uint8)  # all "none"
    fields[0, 1, 2] = 5  # tile 1 = (0, 1): building class 5 -> channel 16 + 5
    fields[0, 3, 0] = 7  # tile 3 = (1, 1): terrain class 7 (fog) -> channel 7
    flags = torch.zeros(1, 4, dtype=torch.uint8)
    flags[0, 2] = 1 << 4  # tile 2 = (1, 0): veteran -> channel 97 + 4
    scalars = torch.zeros(1, 4, 4)
    scalars[0, 0, 3] = 0.5  # tile 0 = (0, 0): unit hp -> channel 102 + 3
    x = model.expand(fields, flags, scalars, side)
    assert x.shape == (1, 106, 2, 2)
    assert x[0, 21, 0, 1] == 1 and x[0, 21].sum() == 1
    assert x[0, 7, 1, 1] == 1
    assert x[0, 101, 1, 0] == 1 and x[0, 97:102].sum() == 1
    assert x[0, 105, 0, 0] == 0.5
    assert torch.all(x[0, :97].sum(0) == 9)  # one class per field on every tile
    assert x[0, 8 + 7, 0, 0] == 1  # tile 0's resource "none" (class 7 of field 1) -> channel 15


def test_policy_output_follows_network_index_order():
    side = 3
    net = model.PolyNet(channels=8, blocks=1).eval()
    with torch.no_grad():
        net.policy_spatial.weight.zero_()
        net.policy_spatial.bias.copy_(torch.arange(332, dtype=torch.float32))
        net.policy_global.weight.zero_()
        net.policy_global.bias.copy_(1000 + torch.arange(130, dtype=torch.float32))
    fields = torch.zeros(1, 9, 9, dtype=torch.uint8)
    logits, value = net(fields, torch.zeros(1, 9, dtype=torch.uint8), torch.zeros(1, 9, 4), torch.zeros(1, 80), side)
    assert logits.shape == (1, 332 * 9 + 130)
    for ch, x, y in [(0, 0, 0), (5, 1, 2), (331, 2, 2), (200, 2, 0)]:
        assert logits[0, (ch * side + x) * side + y] == ch
    assert logits[0, 332 * 9 + 129] == 1129
    assert -1 <= value.item() <= 1


def test_policy_loss_by_hand():
    logits = torch.tensor([[0.0, 9.0, 1.0, 5.0], [2.0, 0.0, 0.0, 0.0]])
    edge_index = torch.tensor([[0, 2], [0, 3]])
    edge_visits = torch.tensor([[3.0, 1.0], [10.0, 99.0]])
    mask = torch.tensor([[True, True], [True, False]])  # decision 1 has one edge; the second is padding
    # decision 0: softmax over logits 0 and 1 -> p = (1/(1+e), e/(1+e)); target (0.75, 0.25)
    p0, p1 = 1 / (1 + math.e), math.e / (1 + math.e)
    expected = (-(0.75 * math.log(p0) + 0.25 * math.log(p1)) + 0.0) / 2  # decision 1: one edge, p = 1
    assert model.policy_loss(logits, edge_index, edge_visits, mask).item() == pytest.approx(expected)
    junk = edge_index.clone()
    junk[1, 1] = 1  # padding points at a large logit: must not matter
    assert model.policy_loss(logits, junk, edge_visits * 1, mask).item() == pytest.approx(expected)


def test_a_small_set_is_learnt():
    torch.manual_seed(0)
    side, n = 3, 32
    fields = torch.stack([torch.randint(0, c, (n, 9)) for c in records.TILE_CLASSES], dim=2).to(torch.uint8)
    flags = torch.randint(0, 32, (n, 9), dtype=torch.uint8)
    scalars, glob = torch.rand(n, 9, 4), torch.rand(n, 80)
    edge_index = torch.randint(0, 332 * 9 + 130, (n, 4))
    edge_visits = torch.rand(n, 4) * 100
    mask = torch.ones(n, 4, dtype=torch.bool)
    z = torch.randint(0, 2, (n,)).float() * 2 - 1
    net = model.PolyNet(channels=16, blocks=1)
    opt = torch.optim.Adam(net.parameters(), lr=0.01)

    def loss() -> torch.Tensor:
        logits, value = net(fields, flags, scalars, glob, side)
        return model.policy_loss(logits, edge_index, edge_visits, mask) + model.value_loss(value, z)

    first = loss().item()
    for _ in range(150):
        opt.zero_grad()
        loss().backward()
        opt.step()
    assert loss().item() < 0.5 * first


def test_spatial_policy_tile_order_is_x_major():
    side = 3

    class Fixed(torch.nn.Module):  # a spatial head output with a distinct value on every (ch, x, y)
        def forward(self, h: torch.Tensor) -> torch.Tensor:
            return (100 * torch.arange(332)[:, None, None] + 10 * torch.arange(side)[None, :, None]
                    + torch.arange(side)[None, None, :]).float().expand(h.shape[0], 332, side, side)

    net = model.PolyNet(channels=8, blocks=1).eval()
    net.policy_spatial = Fixed()
    logits, _ = net(torch.zeros(1, 9, 9, dtype=torch.uint8), torch.zeros(1, 9, dtype=torch.uint8),
                    torch.zeros(1, 9, 4), torch.zeros(1, 80), side)
    for ch, x, y in [(0, 0, 1), (0, 1, 0), (7, 2, 1), (331, 1, 2)]:
        assert logits[0, (ch * side + x) * side + y] == 100 * ch + 10 * x + y
