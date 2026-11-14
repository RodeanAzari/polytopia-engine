# Desktop setup (planned 2026-11-15 morning)

The user's own desktop (RTX 3060 + Ryzen 5 5600, Windows) is at another location on another network.
Goal: the laptop's Claude Code session reaches it over SSH, to copy data, build, train and run self-play.
Route: Chrome Remote Desktop (screen, for the user) → WSL + Tailscale + SSH (shell, for Claude Code).

## Part A: needs someone at the desktop (the user's dad). Do all of it in one go, ~15 min
Have the laptop ready before calling, so nothing waits on us.
1. Desktop on, logged in to Windows, wired network if possible.
2. **Virtualization on** (WSL 2 needs it): Task Manager → Performance → CPU → "Virtualization: Enabled".
   If disabled, it has to be switched on in the BIOS now: the only step that can't be done remotely.
3. **Chrome Remote Desktop, unattended:** the user opens remotedesktop.google.com/headless on the laptop
   (own Google account), gets a one-line command, sends it; the dad pastes it into PowerShell on the desktop
   and enters the PIN the user picks. No password sharing.
4. **Never sleep:** Settings → System → Power → screen and sleep "Never" (plugged in).
5. **Test before he leaves:** the user connects from the laptop via Chrome Remote Desktop and reboots the desktop
   once; it must come back and be reachable again without anyone touching it.
(Optional while there: BIOS "restore on AC power loss" = on, so it restarts after a power cut.)

## Part B: the user via Chrome Remote Desktop, Claude Code writes the steps
1. `wsl --install -d Ubuntu` (PowerShell as admin), reboot, create the Linux user.
2. NVIDIA Windows driver current; `nvidia-smi` works inside WSL.
3. Windows Update active hours set, so it doesn't reboot mid-run.
4. Tailscale on Windows and on the laptop, same account; note the desktop's Tailscale name.
5. In WSL: `openssh-server` running, the laptop's public key in `~/.ssh/authorized_keys`, password login off.
6. From the laptop: `ssh <user>@<tailscale-name>` works. From here on Claude Code does the rest.

## Part C: Claude Code over SSH
1. Laptop: stop gen1 (finished games kept; in-flight lost), note the game count.
2. Clone the repo (pushed 2026-11-14, `ceb645d`); `make setup setup-nn build-nn`; `make build test`; GPU
   parity tests.
3. rsync (resumable) over Tailscale: `data/selfplay/bootstrap` (8.4 GB), `data/selfplay/gen1`, `data/nets`,
   `build/levels`. Time depends on the laptop's upload speed: start early.
4. Run there: train on bootstrap + gen1 (and/or finish gen1); re-run the pipelines A/B on the 3060 first.
5. Record the hardware with every throughput number.
