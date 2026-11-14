# Phase 4 compute plan

Agreed with the user on 2026-11-13 (buildplan §8: "compute plan written before starting"; CLAUDE.md §1).

## Hardware
- **Local laptop (the budget below):** NVIDIA RTX A2000 Laptop GPU, 4 GB, driver 596.52, under WSL2.
  CPU i9-11950H, 16 threads; 31 GB RAM, shared with other projects' sessions. PyTorch 2.14.0+cu130.
  Measured 2026-11-13, 4096² matmul: 3.9 TFLOP/s fp32, 17.2 TFLOP/s fp16.
- **Measured CPU throughput:** about 400 PUCT-vs-PUCT games per hour at 8 jobs (Phase 3 SPRT timing, 2,000
  FM calls per move). Self-play with a network in the loop is `[TBD]`; stage 0 measures it.
- **No cloud spending** under this plan. Cloud GPUs need a separate request to the user, with a dollar figure.
  Prices checked on 2026-11-13: RTX 4090 $0.34–0.89/h, A100 80 GB $1.09–1.39/h, H100 $1.99–2.90/h.
  These are GPU-only listings; the CPU cores that come with them vary by provider.
- **Second machine:** the user's desktop, RTX 3060 + Ryzen 5 5600 (Windows, at another location). Setup
  planned 2026-11-15: `docs/desktop-setup.md`.

## Budget (local, staged)
| Stage | Running time | Go/stop check |
|---|---|---|
| 0. Setup | ~0 GPU | Network/encoding/backend ADRs; measure self-play games/h with the network in the loop |
| 1. Pilot | 24 h | The policy network at least matches the Rule Based priors it replaces (SPRT) |
| 2. Main run | up to 7 days total (~170 GPU-h) | Network-guided search beats the Phase 3 agent (SPRT, the Phase 4 goal) |
| Hard cap | 14 days total (~340 GPU-h) | Only if stage 2 is still improving at day 7. Otherwise stop, write up, decide with the user |

Runs stay resumable and stay at ≤ 8 CPU jobs (other sessions share the machine). The user can pause them at any time.
Running time is logged per run in the notebook, so the budget is checked against measurements, not estimates.
